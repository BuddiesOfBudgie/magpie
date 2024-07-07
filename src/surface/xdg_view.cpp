#include "types.hpp"
#include "view.hpp"

#include "output.hpp"
#include "popup.hpp"
#include "server.hpp"
#include "subsurface.hpp"
#include "surface.hpp"
#include "input/seat.hpp"

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_cursor.h>
#include <wlr/util/log.h>
#include "wlr-wrap-end.hpp"

/* Called when the surface is mapped, or ready to display on-screen. */
static void xdg_toplevel_map_notify(wl_listener* listener, [[maybe_unused]] void* data) {
	wlr_log(WLR_DEBUG, "wlr_xdg_toplevel.events.map(listener=%p, data=%p)", (void*) listener, data);

	XdgView& view = magpie_container_of(listener, view, map);

	view.map();
}

/* Called when the surface is unmapped, and should no longer be shown. */
static void xdg_toplevel_unmap_notify(wl_listener* listener, [[maybe_unused]] void* data) {
	wlr_log(WLR_DEBUG, "wlr_xdg_toplevel.events.unmap(listener=%p, data=%p)", (void*) listener, data);

	XdgView& view = magpie_container_of(listener, view, unmap);

	view.unmap();
}

/* Called when the surface is destroyed and should never be shown again. */
static void xdg_toplevel_destroy_notify(wl_listener* listener, [[maybe_unused]] void* data) {
	wlr_log(WLR_DEBUG, "wlr_xdg_toplevel.events.destroy(listener=%p, data=%p)", (void*) listener, data);

	XdgView& view = magpie_container_of(listener, view, destroy);

	view.server.views.remove(std::dynamic_pointer_cast<View>(view.shared_from_this()));
}

/* This event is raised when a client would like to begin an interactive
 * move, typically because the user clicked on their client-side
 * decorations. Note that a more sophisticated compositor should check the
 * provided serial against a list of button press serials sent to this
 * client, to prevent the client from requesting this whenever they want. */
static void xdg_toplevel_request_move_notify(wl_listener* listener, [[maybe_unused]] void* data) {
	wlr_log(WLR_DEBUG, "wlr_xdg_toplevel.events.request_move(listener=%p, data=%p)", (void*) listener, data);

	XdgView& view = magpie_container_of(listener, view, request_move);

	view.set_placement(VIEW_PLACEMENT_STACKING);
	view.begin_interactive(MAGPIE_CURSOR_MOVE, 0);
}

/* This event is raised when a client would like to begin an interactive
 * resize, typically because the user clicked on their client-side
 * decorations. Note that a more sophisticated compositor should check the
 * provided serial against a list of button press serials sent to this
 * client, to prevent the client from requesting this whenever they want. */
static void xdg_toplevel_request_resize_notify(wl_listener* listener, void* data) {
	wlr_log(WLR_DEBUG, "wlr_xdg_toplevel.events.request_resize(listener=%p, data=%p)", (void*) listener, data);

	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_xdg_toplevel.events.request_resize");
		return;
	}

	XdgView& view = magpie_container_of(listener, view, request_resize);
	const auto* event = static_cast<wlr_xdg_toplevel_resize_event*>(data);

	view.set_placement(VIEW_PLACEMENT_STACKING);
	view.begin_interactive(MAGPIE_CURSOR_RESIZE, event->edges);
}

/* This event is raised when a client would like to maximize itself,
 * typically because the user clicked on the maximize button on
 * client-side decorations. */
static void xdg_toplevel_request_maximize_notify(wl_listener* listener, [[maybe_unused]] void* data) {
	wlr_log(WLR_DEBUG, "wlr_xdg_toplevel.events.request_maximize(listener=%p, data=%p)", (void*) listener, data);

	XdgView& view = magpie_container_of(listener, view, request_maximize);

	view.toggle_maximize();
	wlr_xdg_surface_schedule_configure(view.wlr.base);
}

static void xdg_toplevel_request_fullscreen_notify(wl_listener* listener, [[maybe_unused]] void* data) {
	wlr_log(WLR_DEBUG, "wlr_xdg_toplevel.events.request_fullscreen(listener=%p, data=%p)", (void*) listener, data);

	XdgView& view = magpie_container_of(listener, view, request_fullscreen);

	view.toggle_fullscreen();
	wlr_xdg_surface_schedule_configure(view.wlr.base);
}

static void xdg_toplevel_request_minimize_notify(wl_listener* listener, [[maybe_unused]] void* data) {
	wlr_log(WLR_DEBUG, "wlr_xdg_toplevel.events.request_minimize(listener=%p, data=%p)", (void*) listener, data);

	XdgView& view = magpie_container_of(listener, view, request_minimize);

	view.set_minimized(!view.is_minimized);
	wlr_xdg_surface_schedule_configure(view.wlr.base);
}

static void xdg_toplevel_set_title_notify(wl_listener* listener, [[maybe_unused]] void* data) {
	wlr_log(WLR_DEBUG, "wlr_xdg_toplevel.events.set_title(listener=%p, data=%p)", (void*) listener, data);

	XdgView& view = magpie_container_of(listener, view, set_title);

	view.toplevel_handle->set_title(view.wlr.title);
}

static void xdg_toplevel_set_app_id_notify(wl_listener* listener, [[maybe_unused]] void* data) {
	wlr_log(WLR_DEBUG, "wlr_xdg_toplevel.events.set_app_id(listener=%p, data=%p)", (void*) listener, data);

	XdgView& view = magpie_container_of(listener, view, set_app_id);

	view.toplevel_handle->set_app_id(view.wlr.app_id);
}

static void xdg_toplevel_set_parent_notify(wl_listener* listener, [[maybe_unused]] void* data) {
	wlr_log(WLR_DEBUG, "wlr_xdg_toplevel.events.set_parent(listener=%p, data=%p)", (void*) listener, data);

	XdgView& view = magpie_container_of(listener, view, set_parent);

	if (view.wlr.parent != nullptr) {
		const auto* m_view = dynamic_cast<View*>(static_cast<Surface*>(view.wlr.parent->base->data));
		if (m_view != nullptr) {
			view.toplevel_handle->set_parent(m_view->toplevel_handle);
			return;
		}
	}

	view.toplevel_handle->set_parent({});
}

static void xdg_surface_new_popup_notify(wl_listener* listener, void* data) {
	wlr_log(WLR_DEBUG, "wlr_xdg_toplevel.events.new_popup(listener=%p, data=%p)", (void*) listener, data);

	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_xdg_surface.events.new_popup");
		return;
	}

	XdgView& view = magpie_container_of(listener, view, new_popup);
	view.popups.emplace(std::make_shared<Popup>(view, *static_cast<wlr_xdg_popup*>(data)));
}

static void xdg_surface_new_subsurface_notify(wl_listener* listener, void* data) {
	wlr_log(WLR_DEBUG, "wlr_xdg_toplevel.events.new_subsurface(listener=%p, data=%p)", (void*) listener, data);

	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_xdg_surface.events.new_subsurface");
		return;
	}

	XdgView& view = magpie_container_of(listener, view, new_subsurface);
	view.subsurfaces.emplace(std::make_shared<Subsurface>(view, *static_cast<wlr_subsurface*>(data)));
}

XdgView::XdgView(Server& server, wlr_xdg_toplevel& xdg_toplevel) noexcept
	: listeners(*this), server(server), wlr(xdg_toplevel) {
	auto* scene_tree = wlr_scene_xdg_surface_create(&server.scene->tree, xdg_toplevel.base);
	scene_node = &scene_tree->node;

	wlr_xdg_toplevel_set_wm_capabilities(&xdg_toplevel,
		WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE | WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE |
			WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN);

	scene_node->data = this;
	wlr.base->surface->data = this;

	toplevel_handle.emplace(*this);
	toplevel_handle->set_title(xdg_toplevel.title);
	toplevel_handle->set_app_id(xdg_toplevel.app_id);

	if (xdg_toplevel.parent != nullptr) {
		auto* m_view = dynamic_cast<View*>(static_cast<Surface*>(xdg_toplevel.parent->base->data));
		if (m_view != nullptr) {
			toplevel_handle->set_parent(m_view->toplevel_handle.value());
		}
	}

	listeners.map.notify = xdg_toplevel_map_notify;
	wl_signal_add(&wlr.base->surface->events.map, &listeners.map);
	listeners.unmap.notify = xdg_toplevel_unmap_notify;
	wl_signal_add(&wlr.base->surface->events.unmap, &listeners.unmap);
	listeners.destroy.notify = xdg_toplevel_destroy_notify;
	wl_signal_add(&wlr.base->events.destroy, &listeners.destroy);
	listeners.request_move.notify = xdg_toplevel_request_move_notify;
	wl_signal_add(&wlr.events.request_move, &listeners.request_move);
	listeners.request_resize.notify = xdg_toplevel_request_resize_notify;
	wl_signal_add(&wlr.events.request_resize, &listeners.request_resize);
	listeners.request_maximize.notify = xdg_toplevel_request_maximize_notify;
	wl_signal_add(&wlr.events.request_maximize, &listeners.request_maximize);
	listeners.request_minimize.notify = xdg_toplevel_request_minimize_notify;
	wl_signal_add(&wlr.events.request_minimize, &listeners.request_minimize);
	listeners.request_fullscreen.notify = xdg_toplevel_request_fullscreen_notify;
	wl_signal_add(&wlr.events.request_fullscreen, &listeners.request_fullscreen);
	listeners.set_title.notify = xdg_toplevel_set_title_notify;
	wl_signal_add(&wlr.events.set_title, &listeners.set_title);
	listeners.set_app_id.notify = xdg_toplevel_set_app_id_notify;
	wl_signal_add(&wlr.events.set_app_id, &listeners.set_app_id);
	listeners.set_parent.notify = xdg_toplevel_set_parent_notify;
	wl_signal_add(&wlr.events.set_parent, &listeners.set_parent);
	listeners.new_popup.notify = xdg_surface_new_popup_notify;
	wl_signal_add(&wlr.base->events.new_popup, &listeners.new_popup);
	listeners.new_subsurface.notify = xdg_surface_new_subsurface_notify;
	wl_signal_add(&wlr.base->surface->events.new_subsurface, &listeners.new_subsurface);
}

XdgView::~XdgView() noexcept {
	wl_list_remove(&listeners.map.link);
	wl_list_remove(&listeners.unmap.link);
	wl_list_remove(&listeners.destroy.link);
	wl_list_remove(&listeners.request_move.link);
	wl_list_remove(&listeners.request_resize.link);
	wl_list_remove(&listeners.request_maximize.link);
	wl_list_remove(&listeners.request_minimize.link);
	wl_list_remove(&listeners.set_title.link);
	wl_list_remove(&listeners.set_app_id.link);
	wl_list_remove(&listeners.set_parent.link);
	wl_list_remove(&listeners.new_popup.link);
	wl_list_remove(&listeners.new_subsurface.link);
}

wlr_surface* XdgView::get_wlr_surface() const {
	return wlr.base->surface;
}

Server& XdgView::get_server() const {
	return server;
}

wlr_box XdgView::get_geometry() const {
	wlr_box box = {};
	wlr_xdg_surface_get_geometry(wlr.base, &box);
	return box;
}

wlr_box XdgView::get_min_size() const {
	return {.x = 0, .y = 0, .width = wlr.current.min_width, .height = wlr.current.min_height};
}

wlr_box XdgView::get_max_size() const {
	const int32_t max_width = wlr.current.max_width > 0 ? wlr.current.max_width : INT32_MAX;
	const int32_t max_height = wlr.current.max_height > 0 ? wlr.current.max_height : INT32_MAX;
	return {.x = 0, .y = 0, .width = max_width, .height = max_height};
}

void XdgView::map() {
	if (pending_map) {
		wlr_xdg_surface_get_geometry(wlr.base, &previous);
		wlr_xdg_surface_get_geometry(wlr.base, &current);

		if (!server.outputs.empty()) {
			auto* const output = static_cast<Output*>(wlr_output_layout_get_center_output(server.output_layout)->data);
			const auto usable_area = output->usable_area;
			const auto center_x = usable_area.x + (usable_area.width / 2);
			const auto center_y = usable_area.y + (usable_area.height / 2);
			set_position(center_x - (current.width / 2), center_y - (current.height / 2));
		}

		pending_map = false;
	}

	wlr_scene_node_set_enabled(scene_node, true);
	if (wlr.current.fullscreen) {
		set_placement(VIEW_PLACEMENT_FULLSCREEN);
	} else if (wlr.current.maximized) {
		set_placement(VIEW_PLACEMENT_MAXIMIZED);
	}

	update_outputs(true);

	server.focus_view(std::dynamic_pointer_cast<View>(shared_from_this()));
}

void XdgView::unmap() {
	wlr_scene_node_set_enabled(scene_node, false);

	/* Reset the cursor mode if the grabbed view was unmapped. */
	if (this == server.grabbed_view.lock().get()) {
		server.seat->cursor.reset_mode();
	}

	if (this == server.focused_view.lock().get()) {
		server.focused_view.reset();
	}
}

void XdgView::close() {
	wlr_xdg_toplevel_send_close(&wlr);
}

void XdgView::impl_set_position(const int32_t x, const int32_t y) {
	(void) x;
	(void) y;
}

void XdgView::impl_set_size(const int32_t width, const int32_t height) {
	wlr_xdg_toplevel_set_size(&wlr, width, height);
}

void XdgView::impl_set_geometry(const int32_t x, const int32_t y, const int32_t width, const int32_t height) {
	(void) x;
	(void) y;
	wlr_xdg_toplevel_set_size(&wlr, width, height);
}

void XdgView::impl_set_activated(const bool activated) {
	wlr_xdg_toplevel_set_activated(&wlr, activated);
}

void XdgView::impl_set_fullscreen(const bool fullscreen) {
	wlr_xdg_toplevel_set_fullscreen(&wlr, fullscreen);
}

void XdgView::impl_set_maximized(const bool maximized) {
	wlr_xdg_toplevel_set_maximized(&wlr, maximized);
}

void XdgView::impl_set_minimized(const bool minimized) {
	(void) minimized;
}
