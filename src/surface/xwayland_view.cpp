#include "view.hpp"

#include "foreign_toplevel.hpp"
#include "input/seat.hpp"
#include "output.hpp"
#include "server.hpp"
#include "surface.hpp"
#include "types.hpp"

#include <cstdlib>
#include <wayland-server-core.h>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_seat.h>
#include "wlr-wrap-end.hpp"

/* Called when the surface is mapped, or ready to display on-screen. */
static void xwayland_surface_map_notify(wl_listener* listener, void* data) {
	XWaylandView& view = magpie_container_of(listener, view, map);
	(void) data;

	view.map();
}

/* Called when the surface is unmapped, and should no longer be shown. */
static void xwayland_surface_unmap_notify(wl_listener* listener, void* data) {
	XWaylandView& view = magpie_container_of(listener, view, unmap);
	(void) data;

	view.unmap();
}

/* Called when the surface is destroyed and should never be shown again. */
static void xwayland_surface_destroy_notify(wl_listener* listener, void* data) {
	XWaylandView& view = magpie_container_of(listener, view, destroy);
	(void) data;

	view.server.views.remove(&view);
	delete &view;
}

static void xwayland_surface_request_configure_notify(wl_listener* listener, void* data) {
	XWaylandView& view = magpie_container_of(listener, view, request_configure);
	const auto& event = *static_cast<wlr_xwayland_surface_configure_event*>(data);

	view.set_geometry(event.x, event.y, event.width, event.height);
}

static void xwayland_surface_set_geometry_notify(wl_listener* listener, void* data) {
	XWaylandView& view = magpie_container_of(listener, view, set_geometry);
	(void) data;

	if (view.server.grabbed_view != &view) {
		const wlr_xwayland_surface& surface = view.xwayland_surface;
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
static void xwayland_surface_request_move_notify(wl_listener* listener, void* data) {
	XWaylandView& view = magpie_container_of(listener, view, request_move);
	(void) data;

	view.set_placement(VIEW_PLACEMENT_STACKING);
	view.begin_interactive(MAGPIE_CURSOR_MOVE, 0);
}

/* This event is raised when a client would like to begin an interactive
 * resize, typically because the user clicked on their client-side
 * decorations. Note that a more sophisticated compositor should check the
 * provided serial against a list of button press serials sent to this
 * client, to prevent the client from requesting this whenever they want. */
static void xwayland_surface_request_resize_notify(wl_listener* listener, void* data) {
	XWaylandView& view = magpie_container_of(listener, view, request_resize);
	const auto* event = static_cast<wlr_xwayland_resize_event*>(data);

	view.set_placement(VIEW_PLACEMENT_STACKING);
	view.begin_interactive(MAGPIE_CURSOR_RESIZE, event->edges);
}

static void xwayland_surface_request_maximize_notify(wl_listener* listener, void* data) {
	XWaylandView& view = magpie_container_of(listener, view, request_maximize);
	(void) data;

	view.toggle_maximize();
}

static void xwayland_surface_request_fullscreen_notify(wl_listener* listener, void* data) {
	XWaylandView& view = magpie_container_of(listener, view, request_fullscreen);
	(void) data;

	view.toggle_fullscreen();
}

static void xwayland_surface_set_title_notify(wl_listener* listener, void* data) {
	XWaylandView& view = magpie_container_of(listener, view, set_title);
	(void) data;

	if (view.toplevel_handle.has_value()) {
		view.toplevel_handle->set_title(view.xwayland_surface.title);
	}
}

static void xwayland_surface_set_class_notify(wl_listener* listener, void* data) {
	XWaylandView& view = magpie_container_of(listener, view, set_class);
	(void) data;

	if (view.toplevel_handle.has_value()) {
		view.toplevel_handle->set_app_id(view.xwayland_surface._class);
	}
}

static void xwayland_surface_set_parent_notify(wl_listener* listener, void* data) {
	XWaylandView& view = magpie_container_of(listener, view, set_parent);
	(void) data;

	if (view.xwayland_surface.parent != nullptr) {
		auto* m_view = dynamic_cast<View*>(static_cast<Surface*>(view.xwayland_surface.parent->data));
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
	: listeners(*this), server(server), xwayland_surface(surface) {
	this->xwayland_surface = surface;

	/* Listen to the various events it can emit */
	listeners.map.notify = xwayland_surface_map_notify;
	wl_signal_add(&surface.events.map, &listeners.map);
	listeners.unmap.notify = xwayland_surface_unmap_notify;
	wl_signal_add(&surface.events.unmap, &listeners.unmap);
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
	wl_list_remove(&listeners.map.link);
	wl_list_remove(&listeners.unmap.link);
	wl_list_remove(&listeners.destroy.link);
	wl_list_remove(&listeners.request_configure.link);
	wl_list_remove(&listeners.request_move.link);
	wl_list_remove(&listeners.request_resize.link);
	wl_list_remove(&listeners.set_geometry.link);
	wl_list_remove(&listeners.set_title.link);
	wl_list_remove(&listeners.set_class.link);
	wl_list_remove(&listeners.set_parent.link);
}

constexpr wlr_surface* XWaylandView::get_wlr_surface() const {
	return xwayland_surface.surface;
}

constexpr Server& XWaylandView::get_server() const {
	return server;
}

constexpr wlr_box XWaylandView::get_geometry() const {
	return {xwayland_surface.x, xwayland_surface.y, xwayland_surface.width, xwayland_surface.height};
}

constexpr wlr_box XWaylandView::get_min_size() const {
	wlr_box min = {0, 0, 0, 0};
	if (xwayland_surface.size_hints != nullptr) {
		const auto& hints = *xwayland_surface.size_hints;
		min.width = std::max(hints.min_width, hints.base_width);
		min.height = std::max(hints.min_height, hints.base_height);
	}
	return min;
}

constexpr wlr_box XWaylandView::get_max_size() const {
	wlr_box max = {0, 0, UINT16_MAX, UINT16_MAX};
	if (xwayland_surface.size_hints != nullptr) {
		const auto& hints = *xwayland_surface.size_hints;
		max.width = hints.max_width > 0 ? hints.max_width : UINT16_MAX;
		max.height = hints.max_height > 0 ? hints.max_height : UINT16_MAX;
	}
	return max;
}

void XWaylandView::map() {
	xwayland_surface.data = this;
	xwayland_surface.surface->data = this;

	toplevel_handle.emplace(*this);
	if (xwayland_surface.title != nullptr) {
		toplevel_handle->set_title(xwayland_surface.title);
	}
	if (xwayland_surface._class != nullptr) {
		toplevel_handle->set_app_id(xwayland_surface._class);
	}

	wlr_scene_tree* scene_tree = wlr_scene_subsurface_tree_create(&server.scene->tree, xwayland_surface.surface);
	scene_node = &scene_tree->node;
	scene_node->data = this;

	if (xwayland_surface.parent != nullptr) {
		const auto* m_view = dynamic_cast<View*>(static_cast<Surface*>(xwayland_surface.parent->data));
		if (m_view != nullptr) {
			wlr_scene_node_reparent(scene_node, m_view->scene_node->parent);
			toplevel_handle->set_parent(m_view->toplevel_handle);
		}
	}

	wlr_scene_node_set_enabled(scene_node, true);
	wlr_scene_node_set_position(scene_node, current.x, current.y);

	if (xwayland_surface.fullscreen) {
		set_placement(VIEW_PLACEMENT_FULLSCREEN);
	} else if (xwayland_surface.maximized_horz && xwayland_surface.maximized_vert) {
		set_placement(VIEW_PLACEMENT_MAXIMIZED);
	}

	server.views.insert(server.views.begin(), this);
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

	if (server.seat->wlr->keyboard_state.focused_surface == xwayland_surface.surface) {
		server.seat->wlr->keyboard_state.focused_surface = nullptr;
	}

	server.views.remove(this);

	toplevel_handle.reset();
}

void XWaylandView::close() {
	wlr_xwayland_surface_close(&xwayland_surface);
}

static constexpr int16_t trunc(const int32_t int32) {
	if (int32 > INT16_MAX) {
		return INT16_MAX;
	}

	if (int32 < INT16_MIN) {
		return INT16_MIN;
	}

	return static_cast<int16_t>(int32);
}

void XWaylandView::impl_set_position(const int32_t x, const int32_t y) {
	wlr_xwayland_surface_configure(&xwayland_surface, trunc(x), trunc(y), current.width, current.height);
}

void XWaylandView::impl_set_size(const int32_t width, const int32_t height) {
	wlr_xwayland_surface_configure(&xwayland_surface, trunc(current.x), trunc(current.y), width, height);
}

void XWaylandView::impl_set_geometry(const int32_t x, const int32_t y, const int32_t width, const int32_t height) {
	wlr_xwayland_surface_configure(&xwayland_surface, trunc(x), trunc(y), trunc(width), trunc(height));
}

void XWaylandView::impl_set_activated(const bool activated) {
	wlr_xwayland_surface_activate(&xwayland_surface, activated);
	if (activated) {
		wlr_xwayland_surface_restack(&xwayland_surface, nullptr, XCB_STACK_MODE_ABOVE);
	}
}

void XWaylandView::impl_set_fullscreen(const bool fullscreen) {
	wlr_xwayland_surface_set_fullscreen(&xwayland_surface, fullscreen);
}

void XWaylandView::impl_set_maximized(const bool maximized) {
	wlr_xwayland_surface_set_maximized(&xwayland_surface, maximized);
}

void XWaylandView::impl_set_minimized(const bool minimized) {
	wlr_xwayland_surface_set_minimized(&xwayland_surface, minimized);
}
