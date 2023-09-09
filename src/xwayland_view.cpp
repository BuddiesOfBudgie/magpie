#include "view.hpp"

#include "foreign_toplevel.hpp"
#include "server.hpp"
#include "surface.hpp"
#include "types.hpp"
#include "input/cursor.hpp"
#include "input/seat.hpp"

#include <cstdlib>
#include <wayland-server-core.h>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/edges.h>
#include "wlr-wrap-end.hpp"

static void xwayland_surface_map_notify(wl_listener* listener, void* data) {
	(void) data;

	/* Called when the surface is mapped, or ready to display on-screen. */
	XWaylandView& view = *magpie_container_of(listener, view, map);

	magpie_surface_t* surface = new_magpie_surface_from_view(view);
	view.xwayland_surface->data = surface;
	view.xwayland_surface->surface->data = surface;

	view.surface = view.xwayland_surface->surface;

	view.toplevel_handle = new ForeignToplevelHandle(view);
	view.toplevel_handle->set_title(view.xwayland_surface->title);
	view.toplevel_handle->set_app_id(view.xwayland_surface->_class);

	struct wlr_scene_tree* scene_tree =
		wlr_scene_subsurface_tree_create(&view.server.scene->tree, view.xwayland_surface->surface);
	view.scene_node = &scene_tree->node;
	view.scene_node->data = surface;

	if (view.xwayland_surface->parent != nullptr) {
		magpie_surface_t* m_surface = static_cast<magpie_surface_t*>(view.xwayland_surface->parent->data);
		if (m_surface != nullptr && m_surface->type == MAGPIE_SURFACE_TYPE_VIEW) {
			wlr_scene_node_reparent(view.scene_node, m_surface->view->scene_node->parent);
			view.toplevel_handle->set_parent(m_surface->view->toplevel_handle);
		}
	}

	wlr_scene_node_set_position(view.scene_node, view.current.x, view.current.y);

	view.server.views.insert(view.server.views.begin(), &view);
	view.server.focus_view(view, view.surface);
}

static void xwayland_surface_unmap_notify(wl_listener* listener, void* data) {
	(void) data;

	/* Called when the surface is unmapped, and should no longer be shown. */
	XWaylandView& view = *magpie_container_of(listener, view, unmap);

	Server& server = view.server;
	Cursor& cursor = *server.seat->cursor;

	/* Reset the cursor mode if the grabbed view was unmapped. */
	if (&view == server.grabbed_view) {
		cursor.reset_mode();
	}

	if (server.seat->wlr_seat->keyboard_state.focused_surface == view.surface) {
		server.seat->wlr_seat->keyboard_state.focused_surface = NULL;
	}

	wlr_scene_node_destroy(view.scene_node);
	server.views.remove(&view);

	delete view.toplevel_handle;
	view.toplevel_handle = nullptr;
}

static void xwayland_surface_destroy_notify(wl_listener* listener, void* data) {
	(void) data;

	/* Called when the surface is destroyed and should never be shown again. */
	XWaylandView& view = *magpie_container_of(listener, view, destroy);

	// just in case
	view.server.views.remove(&view);

	delete &view;
}

static void xwayland_surface_request_configure_notify(wl_listener* listener, void* data) {
	XWaylandView& view = *magpie_container_of(listener, view, request_configure);

	struct wlr_xwayland_surface* surface = view.xwayland_surface;
	struct wlr_xwayland_surface_configure_event* event = static_cast<struct wlr_xwayland_surface_configure_event*>(data);

	wlr_xwayland_surface_configure(surface, event->x, event->y, event->width, event->height);
	view.current = {event->x, event->y, event->width, event->height};

	if (surface->mapped) {
		wlr_scene_node_set_position(view.scene_node, event->x, event->y);
	}
}

static void xwayland_surface_set_geometry_notify(wl_listener* listener, void* data) {
	(void) data;

	XWaylandView& view = *magpie_container_of(listener, view, set_geometry);

	struct wlr_xwayland_surface& surface = *view.xwayland_surface;

	view.current = {surface.x, surface.y, surface.width, surface.height};
	if (surface.mapped) {
		wlr_scene_node_set_position(view.scene_node, view.current.x, view.current.y);
	}
}

static void xwayland_surface_request_move_notify(wl_listener* listener, void* data) {
	(void) data;

	/* This event is raised when a client would like to begin an interactive
	 * move, typically because the user clicked on their client-side
	 * decorations. Note that a more sophisticated compositor should check the
	 * provided serial against a list of button press serials sent to this
	 * client, to prevent the client from requesting this whenever they want. */
	XWaylandView& view = *magpie_container_of(listener, view, request_move);

	wlr_xwayland_surface_set_maximized(view.xwayland_surface, false);
	view.begin_interactive(MAGPIE_CURSOR_MOVE, 0);
}

static void xwayland_surface_request_resize_notify(wl_listener* listener, void* data) {
	/* This event is raised when a client would like to begin an interactive
	 * resize, typically because the user clicked on their client-side
	 * decorations. Note that a more sophisticated compositor should check the
	 * provided serial against a list of button press serials sent to this
	 * client, to prevent the client from requesting this whenever they want. */
	XWaylandView& view = *magpie_container_of(listener, view, request_resize);

	struct wlr_xwayland_resize_event* event = static_cast<struct wlr_xwayland_resize_event*>(data);
	wlr_xwayland_surface_set_maximized(view.xwayland_surface, false);
	view.begin_interactive(MAGPIE_CURSOR_RESIZE, event->edges);
}

static void xwayland_surface_set_title_notify(wl_listener* listener, void* data) {
	(void) data;

	XWaylandView& view = *magpie_container_of(listener, view, set_title);

	if (view.toplevel_handle != nullptr) {
		view.toplevel_handle->set_title(view.xwayland_surface->title);
	}
}

static void xwayland_surface_set_class_notify(wl_listener* listener, void* data) {
	(void) data;

	XWaylandView& view = *magpie_container_of(listener, view, set_class);

	if (view.toplevel_handle != nullptr) {
		view.toplevel_handle->set_app_id(view.xwayland_surface->_class);
	}
}

static void xwayland_surface_set_parent_notify(wl_listener* listener, void* data) {
	(void) data;

	XWaylandView& view = *magpie_container_of(listener, view, set_parent);

	if (view.toplevel_handle == nullptr)
		return;

	if (view.xwayland_surface->parent != nullptr) {
		magpie_surface_t* m_surface = static_cast<magpie_surface_t*>(view.xwayland_surface->parent->data);
		if (m_surface != nullptr && m_surface->type == MAGPIE_SURFACE_TYPE_VIEW) {
			wlr_scene_node_reparent(view.scene_node, m_surface->view->scene_node->parent);
			view.toplevel_handle->set_parent(m_surface->view->toplevel_handle);
			return;
		}
	}

	view.toplevel_handle->set_parent(nullptr);
}

XWaylandView::XWaylandView(Server& server, struct wlr_xwayland_surface* xwayland_surface) : server(server) {
	listeners.parent = this;

	this->xwayland_surface = xwayland_surface;
	toplevel_handle = nullptr;

	/* Listen to the various events it can emit */
	listeners.map.notify = xwayland_surface_map_notify;
	wl_signal_add(&xwayland_surface->events.map, &listeners.map);
	listeners.unmap.notify = xwayland_surface_unmap_notify;
	wl_signal_add(&xwayland_surface->events.unmap, &listeners.unmap);
	listeners.destroy.notify = xwayland_surface_destroy_notify;
	wl_signal_add(&xwayland_surface->events.destroy, &listeners.destroy);
	listeners.request_configure.notify = xwayland_surface_request_configure_notify;
	wl_signal_add(&xwayland_surface->events.request_configure, &listeners.request_configure);
	listeners.request_move.notify = xwayland_surface_request_move_notify;
	wl_signal_add(&xwayland_surface->events.request_move, &listeners.request_move);
	listeners.request_resize.notify = xwayland_surface_request_resize_notify;
	wl_signal_add(&xwayland_surface->events.request_resize, &listeners.request_resize);
	listeners.set_geometry.notify = xwayland_surface_set_geometry_notify;
	wl_signal_add(&xwayland_surface->events.set_geometry, &listeners.set_geometry);
	listeners.set_title.notify = xwayland_surface_set_title_notify;
	wl_signal_add(&xwayland_surface->events.set_title, &listeners.set_title);
	listeners.set_class.notify = xwayland_surface_set_class_notify;
	wl_signal_add(&xwayland_surface->events.set_class, &listeners.set_class);
	listeners.set_parent.notify = xwayland_surface_set_parent_notify;
	wl_signal_add(&xwayland_surface->events.set_parent, &listeners.set_parent);
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

Server& XWaylandView::get_server() {
	return server;
}

struct wlr_box XWaylandView::get_geometry() {
	struct wlr_box box;
	box.x = xwayland_surface->x;
	box.y = xwayland_surface->y;
	box.width = xwayland_surface->width;
	box.height = xwayland_surface->height;
	return box;
}

void XWaylandView::set_size(int new_width, int new_height) {
	wlr_xwayland_surface_configure(xwayland_surface, current.x, current.y, new_width, new_height);
}

void XWaylandView::begin_interactive(CursorMode mode, uint32_t edges) {
	Cursor& cursor = *server.seat->cursor;
	struct wlr_surface* focused_surface = server.seat->wlr_seat->pointer_state.focused_surface;

	if (xwayland_surface->surface != wlr_surface_get_root_surface(focused_surface)) {
		/* Deny move/resize requests from unfocused clients. */
		return;
	}

	server.grabbed_view = this;
	cursor.mode = mode;

	if (mode == MAGPIE_CURSOR_MOVE) {
		server.grab_x = cursor.wlr_cursor->x - current.x;
		server.grab_y = cursor.wlr_cursor->y - current.y;
	} else {
		struct wlr_box geo_box = get_geometry();

		double border_x = (current.x + geo_box.x) + ((edges & WLR_EDGE_RIGHT) ? geo_box.width : 0);
		double border_y = (current.y + geo_box.y) + ((edges & WLR_EDGE_BOTTOM) ? geo_box.height : 0);
		server.grab_x = cursor.wlr_cursor->x - border_x;
		server.grab_y = cursor.wlr_cursor->y - border_y;

		server.grab_geobox = geo_box;
		server.grab_geobox.x += current.x;
		server.grab_geobox.y += current.y;

		server.resize_edges = edges;
	}
}

void XWaylandView::set_activated(bool activated) {
	wlr_xwayland_surface_activate(xwayland_surface, activated);
	toplevel_handle->set_activated(activated);
	if (activated) {
		wlr_xwayland_surface_restack(xwayland_surface, NULL, XCB_STACK_MODE_ABOVE);
	}
}

void XWaylandView::set_maximized(bool maximized) {}
