#include "view.hpp"

#include "input/seat.hpp"
#include "output.hpp"
#include "server.hpp"
#include "surface.hpp"
#include "types.hpp"
#include "xwayland.hpp"

#include <cstdlib>
#include <wayland-server-core.h>

#include "wlr-wrap-start.hpp"
#include <wlr/util/log.h>
#include "wlr-wrap-end.hpp"

/* Called when the surface is mapped, or ready to display on-screen. */
static void xwayland_surface_map_notify(wl_listener* listener, void*) {
	XWaylandView& view = magpie_container_of(listener, view, map);

	view.map();
}

/* Called when the surface is unmapped, and should no longer be shown. */
static void xwayland_surface_unmap_notify(wl_listener* listener, void*) {
	XWaylandView& view = magpie_container_of(listener, view, unmap);

	view.unmap();
}

static void xwayland_surface_associate_notify(wl_listener* listener, void*) {
	XWaylandView& view = magpie_container_of(listener, view, associate);

	view.listeners.map.notify = xwayland_surface_map_notify;
	wl_signal_add(&view.wlr.surface->events.map, &view.listeners.map);
	view.listeners.unmap.notify = xwayland_surface_unmap_notify;
	wl_signal_add(&view.wlr.surface->events.unmap, &view.listeners.unmap);
}

static void xwayland_surface_dissociate_notify(wl_listener* listener, void*) {
	XWaylandView& view = magpie_container_of(listener, view, dissociate);

	wl_list_remove(&view.listeners.map.link);
	wl_list_remove(&view.listeners.unmap.link);
}

/* Called when the surface is destroyed and should never be shown again. */
static void xwayland_surface_destroy_notify(wl_listener* listener, void*) {
	XWaylandView& view = magpie_container_of(listener, view, destroy);

	view.server.xwayland->unmapped_views.remove(&view);
	view.server.views.remove(&view);
	delete &view;
}

static void xwayland_surface_request_configure_notify(wl_listener* listener, void* data) {
	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_xwayland_surface.events.request_configure");
		return;
	}

	XWaylandView& view = magpie_container_of(listener, view, request_configure);
	const auto& event = *static_cast<wlr_xwayland_surface_configure_event*>(data);

	view.set_geometry(event.x, event.y, event.width, event.height);
}

static void xwayland_surface_set_geometry_notify(wl_listener* listener, void*) {
	XWaylandView& view = magpie_container_of(listener, view, set_geometry);

	if (view.server.grabbed_view != &view) {
		const wlr_xwayland_surface& surface = view.wlr;
		if (view.curr_placement == VIEW_PLACEMENT_STACKING) {
			view.previous = view.current;
		}
		view.current = {surface.x, surface.y, surface.width, surface.height};
	}
}

/* This event is raised when a client would like to begin an interactive
 * move, typically because the user clicked on their client-side
 * decorations. Note that a more sophisticated compositor should check the
 * provided serial against a list of button press serials sent to this
 * client, to prevent the client from requesting this whenever they want. */
static void xwayland_surface_request_move_notify(wl_listener* listener, void*) {
	XWaylandView& view = magpie_container_of(listener, view, request_move);

	view.set_placement(VIEW_PLACEMENT_STACKING);
	view.begin_interactive(MAGPIE_CURSOR_MOVE, 0);
}

/* This event is raised when a client would like to begin an interactive
 * resize, typically because the user clicked on their client-side
 * decorations. Note that a more sophisticated compositor should check the
 * provided serial against a list of button press serials sent to this
 * client, to prevent the client from requesting this whenever they want. */
static void xwayland_surface_request_resize_notify(wl_listener* listener, void* data) {
	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_xwayland_surface.events.request_resize");
		return;
	}

	XWaylandView& view = magpie_container_of(listener, view, request_resize);
	const auto* event = static_cast<wlr_xwayland_resize_event*>(data);

	view.set_placement(VIEW_PLACEMENT_STACKING);
	view.begin_interactive(MAGPIE_CURSOR_RESIZE, event->edges);
}

static void xwayland_surface_request_maximize_notify(wl_listener* listener, void*) {
	XWaylandView& view = magpie_container_of(listener, view, request_maximize);

	view.toggle_maximize();
}

static void xwayland_surface_request_fullscreen_notify(wl_listener* listener, void*) {
	XWaylandView& view = magpie_container_of(listener, view, request_fullscreen);

	view.toggle_fullscreen();
}

static void xwayland_surface_set_title_notify(wl_listener* listener, void*) {
	XWaylandView& view = magpie_container_of(listener, view, set_title);

	if (view.toplevel_handle.has_value()) {
		view.toplevel_handle->set_title(view.wlr.title);
	}
}

static void xwayland_surface_set_class_notify(wl_listener* listener, void*) {
	XWaylandView& view = magpie_container_of(listener, view, set_class);

	if (view.toplevel_handle.has_value()) {
		view.toplevel_handle->set_app_id(view.wlr._class);
	}
}

static void xwayland_surface_set_parent_notify(wl_listener* listener, void*) {
	XWaylandView& view = magpie_container_of(listener, view, set_parent);

	if (view.wlr.parent != nullptr) {
		auto* m_view = dynamic_cast<View*>(static_cast<Surface*>(view.wlr.parent->data));
		if (m_view != nullptr && view.scene_node != nullptr) {
			wlr_scene_node_reparent(view.scene_node, m_view->scene_node->parent);
			if (view.toplevel_handle.has_value() && m_view->toplevel_handle.has_value()) {
				view.toplevel_handle->set_parent(m_view->toplevel_handle.value());
			}
			return;
		}
	}

	if (view.toplevel_handle.has_value()) {
		view.toplevel_handle->set_parent({});
	}
}

XWaylandView::XWaylandView(Server& server, wlr_xwayland_surface& surface) noexcept
	: listeners(*this), server(server), wlr(surface) {
	listeners.associate.notify = xwayland_surface_associate_notify;
	wl_signal_add(&surface.events.associate, &listeners.associate);
	listeners.dissociate.notify = xwayland_surface_dissociate_notify;
	wl_signal_add(&surface.events.dissociate, &listeners.dissociate);
	listeners.destroy.notify = xwayland_surface_destroy_notify;
	wl_signal_add(&surface.events.destroy, &listeners.destroy);
	listeners.request_configure.notify = xwayland_surface_request_configure_notify;
	wl_signal_add(&surface.events.request_configure, &listeners.request_configure);
	listeners.request_move.notify = xwayland_surface_request_move_notify;
	wl_signal_add(&surface.events.request_move, &listeners.request_move);
	listeners.request_resize.notify = xwayland_surface_request_resize_notify;
	wl_signal_add(&surface.events.request_resize, &listeners.request_resize);
	listeners.request_maximize.notify = xwayland_surface_request_maximize_notify;
	wl_signal_add(&surface.events.request_maximize, &listeners.request_maximize);
	listeners.request_fullscreen.notify = xwayland_surface_request_fullscreen_notify;
	wl_signal_add(&surface.events.request_fullscreen, &listeners.request_fullscreen);
	listeners.set_geometry.notify = xwayland_surface_set_geometry_notify;
	wl_signal_add(&surface.events.set_geometry, &listeners.set_geometry);
	listeners.set_title.notify = xwayland_surface_set_title_notify;
	wl_signal_add(&surface.events.set_title, &listeners.set_title);
	listeners.set_class.notify = xwayland_surface_set_class_notify;
	wl_signal_add(&surface.events.set_class, &listeners.set_class);
	listeners.set_parent.notify = xwayland_surface_set_parent_notify;
	wl_signal_add(&surface.events.set_parent, &listeners.set_parent);
}

XWaylandView::~XWaylandView() noexcept {
	wl_list_remove(&listeners.associate.link);
	wl_list_remove(&listeners.destroy.link);
	wl_list_remove(&listeners.request_configure.link);
	wl_list_remove(&listeners.request_move.link);
	wl_list_remove(&listeners.request_resize.link);
	wl_list_remove(&listeners.set_geometry.link);
	wl_list_remove(&listeners.set_title.link);
	wl_list_remove(&listeners.set_class.link);
	wl_list_remove(&listeners.set_parent.link);
}

wlr_surface* XWaylandView::get_wlr_surface() const {
	return wlr.surface;
}

Server& XWaylandView::get_server() const {
	return server;
}

wlr_box XWaylandView::get_geometry() const {
	return {wlr.x, wlr.y, wlr.width, wlr.height};
}

wlr_box XWaylandView::get_min_size() const {
	wlr_box min = {0, 0, 0, 0};
	if (wlr.size_hints != nullptr) {
		const auto& hints = *wlr.size_hints;
		min.width = std::max(hints.min_width, hints.base_width);
		min.height = std::max(hints.min_height, hints.base_height);
	}
	return min;
}

wlr_box XWaylandView::get_max_size() const {
	wlr_box max = {0, 0, UINT16_MAX, UINT16_MAX};
	if (wlr.size_hints != nullptr) {
		const auto& hints = *wlr.size_hints;
		max.width = hints.max_width > 0 ? hints.max_width : UINT16_MAX;
		max.height = hints.max_height > 0 ? hints.max_height : UINT16_MAX;
	}
	return max;
}

void XWaylandView::map() {
	wlr.data = this;
	wlr.surface->data = this;

	toplevel_handle.emplace(*this);
	toplevel_handle->set_title(wlr.title);
	toplevel_handle->set_app_id(wlr._class);

	wlr_scene_tree* scene_tree = wlr_scene_subsurface_tree_create(&server.scene->tree, wlr.surface);
	scene_node = &scene_tree->node;
	scene_node->data = this;

	if (wlr.parent != nullptr) {
		const auto* m_view = dynamic_cast<View*>(static_cast<Surface*>(wlr.parent->data));
		if (m_view != nullptr) {
			wlr_scene_node_reparent(scene_node, m_view->scene_node->parent);
			toplevel_handle->set_parent(m_view->toplevel_handle);
		}
	}

	wlr_scene_node_set_enabled(scene_node, true);
	wlr_scene_node_set_position(scene_node, current.x, current.y);

	if (wlr.fullscreen) {
		set_placement(VIEW_PLACEMENT_FULLSCREEN);
	} else if (wlr.maximized_horz && wlr.maximized_vert) {
		set_placement(VIEW_PLACEMENT_MAXIMIZED);
	}

	server.xwayland->unmapped_views.remove(this);
	server.views.emplace_back(this);
	update_outputs(true);
	server.focus_view(this);
}

void XWaylandView::unmap() {
	wlr_scene_node_set_enabled(scene_node, false);
	wlr_scene_node_destroy(scene_node);
	scene_node = nullptr;
	Cursor& cursor = server.seat->cursor;

	/* Reset the cursor mode if the grabbed view was unmapped. */
	if (this == server.grabbed_view) {
		cursor.reset_mode();
	}

	if (this == server.focused_view) {
		server.focused_view = nullptr;
	}

	if (server.seat->wlr->keyboard_state.focused_surface == wlr.surface) {
		server.seat->wlr->keyboard_state.focused_surface = nullptr;
	}

	server.views.remove(this);
	server.xwayland->unmapped_views.emplace_back(this);

	toplevel_handle.reset();
}

void XWaylandView::close() {
	wlr_xwayland_surface_close(&wlr);
}

static int16_t trunc(const int32_t int32) {
	if (int32 > INT16_MAX) {
		return INT16_MAX;
	}

	if (int32 < INT16_MIN) {
		return INT16_MIN;
	}

	return static_cast<int16_t>(int32);
}

void XWaylandView::impl_set_position(const int32_t x, const int32_t y) {
	wlr_xwayland_surface_configure(&wlr, trunc(x), trunc(y), current.width, current.height);
}

void XWaylandView::impl_set_size(const int32_t width, const int32_t height) {
	wlr_xwayland_surface_configure(&wlr, trunc(current.x), trunc(current.y), width, height);
}

void XWaylandView::impl_set_geometry(const int32_t x, const int32_t y, const int32_t width, const int32_t height) {
	wlr_xwayland_surface_configure(&wlr, trunc(x), trunc(y), trunc(width), trunc(height));
}

void XWaylandView::impl_set_activated(const bool activated) {
	wlr_xwayland_surface_activate(&wlr, activated);
	if (activated) {
		wlr_xwayland_surface_restack(&wlr, nullptr, XCB_STACK_MODE_ABOVE);
	}
}

void XWaylandView::impl_set_fullscreen(const bool fullscreen) {
	wlr_xwayland_surface_set_fullscreen(&wlr, fullscreen);
}

void XWaylandView::impl_set_maximized(const bool maximized) {
	wlr_xwayland_surface_set_maximized(&wlr, maximized);
}

void XWaylandView::impl_set_minimized(const bool minimized) {
	wlr_xwayland_surface_set_minimized(&wlr, minimized);
}
