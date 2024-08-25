#include "popup.hpp"

#include "output.hpp"
#include "server.hpp"
#include "subsurface.hpp"
#include "surface.hpp"
#include "types.hpp"

#include <utility>

#include "wlr-wrap-start.hpp"
#include <wlr/util/log.h>
#include "wlr-wrap-end.hpp"

static void popup_map_notify(wl_listener* listener, [[maybe_unused]] void* data) {
	wlr_log(WLR_DEBUG, "wlr_xdg_popup.events.map(listener=%p, data=%p)", (void*) listener, data);

	Popup& popup = magpie_container_of(listener, popup, map);

	wlr_box current = {};
	wlr_xdg_surface_get_geometry(popup.wlr->base, &current);

	for (const auto& output : std::as_const(popup.server.outputs)) {
		wlr_box output_area = output->full_area;
		wlr_box intersect = {};
		wlr_box_intersection(&intersect, &current, &output_area);

		if (!wlr_box_empty(&current)) {
			wlr_surface_send_enter(popup.wlr->base->surface, &output->wlr);
		}
	}

	wlr_scene_node_raise_to_top(&popup.scene_tree->node);
}

static void popup_destroy_notify(wl_listener* listener, [[maybe_unused]] void* data) {
	wlr_log(WLR_DEBUG, "wlr_xdg_popup.events.destroy(listener=%p, data=%p)", (void*) listener, data);

	Popup& popup = magpie_container_of(listener, popup, destroy);

	popup.wlr = nullptr;
	popup.parent.popups.erase(std::dynamic_pointer_cast<Popup>(popup.shared_from_this()));
}

static void popup_new_popup_notify(wl_listener* listener, void* data) {
	wlr_log(WLR_DEBUG, "wlr_xdg_popup.events.new_popup(listener=%p, data=%p)", (void*) listener, data);

	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_xdg_popup.events.new_popup");
		return;
	}

	Popup& popup = magpie_container_of(listener, popup, new_popup);
	popup.popups.emplace(std::make_shared<Popup>(popup, *static_cast<wlr_xdg_popup*>(data)));
}

static void popup_new_subsurface_notify(wl_listener* listener, void* data) {
	wlr_log(WLR_DEBUG, "wlr_xdg_popup.events.new_subsurface(listener=%p, data=%p)", (void*) listener, data);

	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_xdg_popup.events.new_subsurface");
		return;
	}

	Popup& popup = magpie_container_of(listener, popup, new_subsurface);
	popup.subsurfaces.emplace(std::make_shared<Subsurface>(popup, *static_cast<wlr_subsurface*>(data)));
}

Popup::Popup(Surface& parent, wlr_xdg_popup& wlr) noexcept
	: listeners(*this), server(parent.get_server()), parent(parent), wlr(&wlr) {
	scene_tree = wlr_scene_xdg_surface_create(parent.scene_tree, wlr.base);
	surface_node = &scene_tree->node;

	surface_node->data = this;
	wlr.base->surface->data = this;

	// just in case the popup hasn't been configured already (2024-07-13)
	wlr_xdg_surface_schedule_configure(wlr.base);

	listeners.map.notify = popup_map_notify;
	wl_signal_add(&wlr.base->surface->events.map, &listeners.map);
	listeners.destroy.notify = popup_destroy_notify;
	wl_signal_add(&wlr.base->events.destroy, &listeners.destroy);
	listeners.new_popup.notify = popup_new_popup_notify;
	wl_signal_add(&wlr.base->events.new_popup, &listeners.new_popup);
	listeners.new_subsurface.notify = popup_new_subsurface_notify;
	wl_signal_add(&wlr.base->surface->events.new_subsurface, &listeners.new_subsurface);
}

Popup::~Popup() noexcept {
	wl_list_remove(&listeners.map.link);
	wl_list_remove(&listeners.destroy.link);
	wl_list_remove(&listeners.new_popup.link);
	wl_list_remove(&listeners.new_subsurface.link);
	if (wlr != nullptr) {
		wlr_xdg_popup_destroy(wlr);
	}
}

wlr_surface* Popup::get_wlr_surface() const {
	return wlr->base->surface;
}

Server& Popup::get_server() const {
	return server;
}

bool Popup::is_view() const {
	return false;
}
