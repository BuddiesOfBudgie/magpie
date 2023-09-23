#include "view.hpp"

#include "foreign_toplevel.hpp"
#include "server.hpp"
#include "surface.hpp"
#include "input/seat.hpp"

#include "wlr-wrap-start.hpp"
#include <wlr/backend.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/util/edges.h>
#include "wlr-wrap-end.hpp"

/* Called when the surface is mapped, or ready to display on-screen. */
static void xdg_toplevel_map_notify(wl_listener* listener, void* data) {
	XdgView& view = magpie_container_of(listener, view, map);
	(void) data;

	view.map();
}

/* Called when the surface is unmapped, and should no longer be shown. */
static void xdg_toplevel_unmap_notify(wl_listener* listener, void* data) {
	XdgView& view = magpie_container_of(listener, view, unmap);
	(void) data;

	view.unmap();
}

/* Called when the surface is destroyed and should never be shown again. */
static void xdg_toplevel_destroy_notify(wl_listener* listener, void* data) {
	XdgView& view = magpie_container_of(listener, view, destroy);
	(void) data;

	view.server.views.remove(&view);
	delete &view;
}

/* This event is raised when a client would like to begin an interactive
 * move, typically because the user clicked on their client-side
 * decorations. Note that a more sophisticated compositor should check the
 * provided serial against a list of button press serials sent to this
 * client, to prevent the client from requesting this whenever they want. */
static void xdg_toplevel_request_move_notify(wl_listener* listener, void* data) {
	XdgView& view = magpie_container_of(listener, view, request_move);
	(void) data;

	view.set_maximized(false);
	view.begin_interactive(MAGPIE_CURSOR_MOVE, 0);
}

/* This event is raised when a client would like to begin an interactive
 * resize, typically because the user clicked on their client-side
 * decorations. Note that a more sophisticated compositor should check the
 * provided serial against a list of button press serials sent to this
 * client, to prevent the client from requesting this whenever they want. */
static void xdg_toplevel_request_resize_notify(wl_listener* listener, void* data) {
	XdgView& view = magpie_container_of(listener, view, request_resize);
	auto* event = static_cast<wlr_xdg_toplevel_resize_event*>(data);

	view.set_maximized(false);
	view.begin_interactive(MAGPIE_CURSOR_RESIZE, event->edges);
}

/* This event is raised when a client would like to maximize itself,
 * typically because the user clicked on the maximize button on
 * client-side decorations. */
static void xdg_toplevel_request_maximize_notify(wl_listener* listener, void* data) {
	XdgView& view = magpie_container_of(listener, view, request_maximize);
	(void) data;

	view.set_maximized(!view.maximized);
	wlr_xdg_surface_schedule_configure(view.xdg_toplevel->base);
}

static void xdg_toplevel_request_fullscreen_notify(wl_listener* listener, void* data) {
	XdgView& view = magpie_container_of(listener, view, request_fullscreen);
	(void) data;

	/* We must send a configure here, even on a no-op. */
	wlr_xdg_surface_schedule_configure(view.xdg_toplevel->base);
}

static void xdg_toplevel_set_title_notify(wl_listener* listener, void* data) {
	XdgView& view = magpie_container_of(listener, view, set_title);
	(void) data;

	view.toplevel_handle->set_title(view.xdg_toplevel->title);
}

static void xdg_toplevel_set_app_id_notify(wl_listener* listener, void* data) {
	XdgView& view = magpie_container_of(listener, view, set_app_id);
	(void) data;

	view.toplevel_handle->set_app_id(view.xdg_toplevel->app_id);
}

static void xdg_toplevel_set_parent_notify(wl_listener* listener, void* data) {
	XdgView& view = magpie_container_of(listener, view, set_parent);
	(void) data;

	if (view.xdg_toplevel->parent != nullptr) {
		auto* m_surface = static_cast<Surface*>(view.xdg_toplevel->parent->base->data);
		if (m_surface != nullptr && m_surface->type == MAGPIE_SURFACE_TYPE_VIEW) {
			view.toplevel_handle->set_parent(m_surface->view.get().toplevel_handle);
			return;
		}
	}

	view.toplevel_handle->set_parent(nullptr);
}

XdgView::XdgView(Server& server, wlr_xdg_toplevel* toplevel) noexcept : listeners(*this), server(server) {
	auto* scene_tree = wlr_scene_xdg_surface_create(&server.scene->tree, toplevel->base);
	scene_node = &scene_tree->node;
	surface = toplevel->base->surface;

	wlr_xdg_surface_get_geometry(toplevel->base, &previous);
	wlr_xdg_toplevel_set_wm_capabilities(toplevel, WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE);

	Surface* surface = new Surface(*this);
	scene_node->data = surface;
	toplevel->base->surface->data = surface;

	xdg_toplevel = toplevel;
	toplevel_handle = new ForeignToplevelHandle(*this);
	toplevel_handle->set_title(xdg_toplevel->title);
	toplevel_handle->set_app_id(xdg_toplevel->app_id);

	if (xdg_toplevel->parent != nullptr) {
		auto* m_surface = static_cast<Surface*>(xdg_toplevel->parent->base->data);
		if (m_surface != nullptr && m_surface->type == MAGPIE_SURFACE_TYPE_VIEW) {
			toplevel_handle->set_parent(m_surface->view.get().toplevel_handle);
		}
	}

	listeners.map.notify = xdg_toplevel_map_notify;
	wl_signal_add(&toplevel->base->events.map, &listeners.map);
	listeners.unmap.notify = xdg_toplevel_unmap_notify;
	wl_signal_add(&toplevel->base->events.unmap, &listeners.unmap);
	listeners.destroy.notify = xdg_toplevel_destroy_notify;
	wl_signal_add(&toplevel->base->events.destroy, &listeners.destroy);
	listeners.request_move.notify = xdg_toplevel_request_move_notify;
	wl_signal_add(&xdg_toplevel->events.request_move, &listeners.request_move);
	listeners.request_resize.notify = xdg_toplevel_request_resize_notify;
	wl_signal_add(&xdg_toplevel->events.request_resize, &listeners.request_resize);
	listeners.request_maximize.notify = xdg_toplevel_request_maximize_notify;
	wl_signal_add(&xdg_toplevel->events.request_maximize, &listeners.request_maximize);
	listeners.request_fullscreen.notify = xdg_toplevel_request_fullscreen_notify;
	wl_signal_add(&xdg_toplevel->events.request_fullscreen, &listeners.request_fullscreen);
	listeners.set_title.notify = xdg_toplevel_set_title_notify;
	wl_signal_add(&xdg_toplevel->events.set_title, &listeners.set_title);
	listeners.set_app_id.notify = xdg_toplevel_set_app_id_notify;
	wl_signal_add(&xdg_toplevel->events.set_app_id, &listeners.set_app_id);
	listeners.set_parent.notify = xdg_toplevel_set_parent_notify;
	wl_signal_add(&xdg_toplevel->events.set_parent, &listeners.set_parent);

	server.views.push_back(this);
}

XdgView::~XdgView() noexcept {
	wl_list_remove(&listeners.map.link);
	wl_list_remove(&listeners.unmap.link);
	wl_list_remove(&listeners.destroy.link);
	wl_list_remove(&listeners.request_move.link);
	wl_list_remove(&listeners.request_resize.link);
	wl_list_remove(&listeners.request_maximize.link);
	wl_list_remove(&listeners.set_title.link);
	wl_list_remove(&listeners.set_app_id.link);
	wl_list_remove(&listeners.set_parent.link);
}

inline Server& XdgView::get_server() {
	return server;
}

const wlr_box XdgView::get_geometry() {
	wlr_box box;
	wlr_xdg_surface_get_geometry(xdg_toplevel->base, &box);
	return box;
}

void XdgView::map() {
	wlr_scene_node_set_enabled(scene_node, true);
	maximized = xdg_toplevel->current.maximized;
	server.focus_view(*this, xdg_toplevel->base->surface);
}

void XdgView::unmap() {
	wlr_scene_node_set_enabled(scene_node, false);

	/* Reset the cursor mode if the grabbed view was unmapped. */
	if (this == server.grabbed_view) {
		server.seat->cursor->reset_mode();
	}
}

void XdgView::impl_set_size(const int new_width, const int new_height) {
	wlr_xdg_toplevel_set_size(xdg_toplevel, new_width, new_height);
}

void XdgView::impl_set_activated(const bool activated) {
	wlr_xdg_toplevel_set_activated(xdg_toplevel, activated);
}

void XdgView::impl_set_maximized(const bool maximized) {
	wlr_xdg_toplevel_set_maximized(xdg_toplevel, maximized);
}
