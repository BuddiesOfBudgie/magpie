#include "view.hpp"

#include "foreign_toplevel.hpp"
#include "server.hpp"
#include "surface.hpp"
#include "types.hpp"
#include "input/seat.hpp"

#include <cstdlib>
#include <wayland-server-core.h>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/edges.h>
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

	wlr_xwayland_surface& surface = view.xwayland_surface;
	auto* event = static_cast<wlr_xwayland_surface_configure_event*>(data);

	wlr_xwayland_surface_configure(&surface, event->x, event->y, event->width, event->height);
	view.current = {event->x, event->y, event->width, event->height};

	if (surface.mapped) {
		wlr_scene_node_set_position(view.scene_node, event->x, event->y);
	}
}

static void xwayland_surface_set_geometry_notify(wl_listener* listener, void* data) {
	XWaylandView& view = magpie_container_of(listener, view, set_geometry);
	(void) data;

	wlr_xwayland_surface& surface = view.xwayland_surface;

	view.current = {surface.x, surface.y, surface.width, surface.height};
	if (surface.mapped) {
		wlr_scene_node_set_position(view.scene_node, view.current.x, view.current.y);
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

	wlr_xwayland_surface_set_maximized(&view.xwayland_surface, false);
	view.begin_interactive(MAGPIE_CURSOR_MOVE, 0);
}

/* This event is raised when a client would like to begin an interactive
 * resize, typically because the user clicked on their client-side
 * decorations. Note that a more sophisticated compositor should check the
 * provided serial against a list of button press serials sent to this
 * client, to prevent the client from requesting this whenever they want. */
static void xwayland_surface_request_resize_notify(wl_listener* listener, void* data) {
	XWaylandView& view = magpie_container_of(listener, view, request_resize);

	auto* event = static_cast<wlr_xwayland_resize_event*>(data);
	wlr_xwayland_surface_set_maximized(&view.xwayland_surface, false);
	view.begin_interactive(MAGPIE_CURSOR_RESIZE, event->edges);
}

static void xwayland_surface_set_title_notify(wl_listener* listener, void* data) {
	XWaylandView& view = magpie_container_of(listener, view, set_title);
	(void) data;

	if (view.toplevel_handle != nullptr) {
		view.toplevel_handle->set_title(view.xwayland_surface.title);
	}
}

static void xwayland_surface_set_class_notify(wl_listener* listener, void* data) {
	XWaylandView& view = magpie_container_of(listener, view, set_class);
	(void) data;

	if (view.toplevel_handle != nullptr) {
		view.toplevel_handle->set_app_id(view.xwayland_surface._class);
	}
}

static void xwayland_surface_set_parent_notify(wl_listener* listener, void* data) {
	XWaylandView& view = magpie_container_of(listener, view, set_parent);
	(void) data;

	if (view.toplevel_handle == nullptr)
		return;

	if (view.xwayland_surface.parent != nullptr) {
		auto* m_view = dynamic_cast<View*>(static_cast<Surface*>(view.xwayland_surface.parent->data));
		if (m_view != nullptr) {
			wlr_scene_node_reparent(view.scene_node, m_view->scene_node->parent);
			view.toplevel_handle->set_parent(m_view->toplevel_handle);
			return;
		}
	}

	view.toplevel_handle->set_parent(nullptr);
}

XWaylandView::XWaylandView(Server& server, wlr_xwayland_surface& xwayland_surface) noexcept
	: listeners(*this), server(server), xwayland_surface(xwayland_surface) {
	this->xwayland_surface = xwayland_surface;
	toplevel_handle = nullptr;

	/* Listen to the various events it can emit */
	listeners.map.notify = xwayland_surface_map_notify;
	wl_signal_add(&xwayland_surface.events.map, &listeners.map);
	listeners.unmap.notify = xwayland_surface_unmap_notify;
	wl_signal_add(&xwayland_surface.events.unmap, &listeners.unmap);
	listeners.destroy.notify = xwayland_surface_destroy_notify;
	wl_signal_add(&xwayland_surface.events.destroy, &listeners.destroy);
	listeners.request_configure.notify = xwayland_surface_request_configure_notify;
	wl_signal_add(&xwayland_surface.events.request_configure, &listeners.request_configure);
	listeners.request_move.notify = xwayland_surface_request_move_notify;
	wl_signal_add(&xwayland_surface.events.request_move, &listeners.request_move);
	listeners.request_resize.notify = xwayland_surface_request_resize_notify;
	wl_signal_add(&xwayland_surface.events.request_resize, &listeners.request_resize);
	listeners.set_geometry.notify = xwayland_surface_set_geometry_notify;
	wl_signal_add(&xwayland_surface.events.set_geometry, &listeners.set_geometry);
	listeners.set_title.notify = xwayland_surface_set_title_notify;
	wl_signal_add(&xwayland_surface.events.set_title, &listeners.set_title);
	listeners.set_class.notify = xwayland_surface_set_class_notify;
	wl_signal_add(&xwayland_surface.events.set_class, &listeners.set_class);
	listeners.set_parent.notify = xwayland_surface_set_parent_notify;
	wl_signal_add(&xwayland_surface.events.set_parent, &listeners.set_parent);
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

inline Server& XWaylandView::get_server() const {
	return server;
}

const wlr_box XWaylandView::get_geometry() const {
	wlr_box box;
	box.x = xwayland_surface.x;
	box.y = xwayland_surface.y;
	box.width = xwayland_surface.width;
	box.height = xwayland_surface.height;
	return box;
}

void XWaylandView::map() {
	xwayland_surface.data = this;
	xwayland_surface.surface->data = this;

	this->surface = xwayland_surface.surface;

	toplevel_handle = new ForeignToplevelHandle(*this);
	toplevel_handle->set_title(xwayland_surface.title);
	toplevel_handle->set_app_id(xwayland_surface._class);

	wlr_scene_tree* scene_tree = wlr_scene_subsurface_tree_create(&server.scene->tree, xwayland_surface.surface);
	scene_node = &scene_tree->node;
	scene_node->data = surface;

	if (xwayland_surface.parent != nullptr) {
		auto* m_view = dynamic_cast<View*>(static_cast<Surface*>(xwayland_surface.parent->data));
		if (m_view != nullptr) {
			wlr_scene_node_reparent(scene_node, m_view->scene_node->parent);
			toplevel_handle->set_parent(m_view->toplevel_handle);
		}
	}

	wlr_scene_node_set_position(scene_node, current.x, current.y);

	server.views.insert(server.views.begin(), this);
	server.focus_view(*this, this->surface);
}

void XWaylandView::unmap() {
	Cursor& cursor = server.seat->cursor;

	/* Reset the cursor mode if the grabbed view was unmapped. */
	if (this == server.grabbed_view) {
		cursor.reset_mode();
	}

	if (server.seat->seat->keyboard_state.focused_surface == surface) {
		server.seat->seat->keyboard_state.focused_surface = NULL;
	}

	wlr_scene_node_destroy(scene_node);
	server.views.remove(this);

	delete toplevel_handle;
	toplevel_handle = nullptr;
}

void XWaylandView::impl_set_size(const int new_width, const int new_height) {
	wlr_xwayland_surface_configure(&xwayland_surface, current.x, current.y, new_width, new_height);
}

void XWaylandView::impl_set_activated(bool activated) {
	wlr_xwayland_surface_activate(&xwayland_surface, activated);
	if (activated) {
		wlr_xwayland_surface_restack(&xwayland_surface, NULL, XCB_STACK_MODE_ABOVE);
	}
}

void XWaylandView::impl_set_maximized(const bool maximized) {
	wlr_xwayland_surface_set_maximized(&xwayland_surface, maximized);
}
