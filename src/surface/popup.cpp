#include "popup.hpp"

#include "output.hpp"
#include "server.hpp"
#include "surface.hpp"
#include "types.hpp"

#include <utility>

#include "wlr-wrap-start.hpp"
#include <wlr/util/log.h>
#include "wlr-wrap-end.hpp"

static void popup_map_notify(wl_listener* listener, void*) {
	Popup& popup = magpie_container_of(listener, popup, map);

	wlr_box current = {};
	wlr_xdg_surface_get_geometry(popup.wlr.base, &current);

	for (auto& output : std::as_const(popup.server.outputs)) {
		wlr_box output_area = output->full_area;
		wlr_box intersect = {};
		wlr_box_intersection(&intersect, &current, &output_area);

		if (!wlr_box_empty(&current)) {
			wlr_surface_send_enter(popup.wlr.base->surface, &output->wlr);
		}
	}
}

static void popup_destroy_notify(wl_listener* listener, void*) {
	Popup& popup = magpie_container_of(listener, popup, destroy);

	popup.parent.popups.erase(&popup);
	delete &popup;
}

static void popup_new_popup_notify(wl_listener* listener, void* data) {
	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_layer_surface_v1.events.new_popup");
		return;
	}

	Popup& popup = magpie_container_of(listener, popup, new_popup);
	popup.popups.emplace(new Popup(popup, *static_cast<wlr_xdg_popup*>(data)));
}

Popup::Popup(Surface& parent, wlr_xdg_popup& wlr) noexcept
	: listeners(*this), server(parent.get_server()), parent(parent), wlr(wlr) {
	this->wlr = wlr;
	auto* scene_tree = wlr_scene_xdg_surface_create(parent.scene_node->parent, wlr.base);
	scene_node = &scene_tree->node;

	scene_node->data = this;
	wlr.base->surface->data = this;

	listeners.map.notify = popup_map_notify;
	wl_signal_add(&wlr.base->surface->events.map, &listeners.map);
	listeners.destroy.notify = popup_destroy_notify;
	wl_signal_add(&wlr.base->events.destroy, &listeners.destroy);
	listeners.new_popup.notify = popup_new_popup_notify;
	wl_signal_add(&wlr.base->events.new_popup, &listeners.new_popup);
}

Popup::~Popup() noexcept {
	wl_list_remove(&listeners.map.link);
	wl_list_remove(&listeners.destroy.link);
	wl_list_remove(&listeners.new_popup.link);
}

wlr_surface* Popup::get_wlr_surface() const {
	return wlr.base->surface;
}

Server& Popup::get_server() const {
	return server;
}

bool Popup::is_view() const {
	return false;
}
