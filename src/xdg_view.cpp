#include "view.hpp"

#include "foreign_toplevel.hpp"
#include "output.hpp"
#include "server.hpp"
#include "surface.hpp"
#include "types.hpp"
#include "input/seat.hpp"
#include "input/cursor.hpp"

#include <algorithm>
#include <cstdlib>

#include "wlr-wrap-start.hpp"
#include <wlr/backend.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>
#include "wlr-wrap-end.hpp"

static void xdg_toplevel_map_notify(wl_listener* listener, void* data) {
	(void) data;

	/* Called when the surface is mapped, or ready to display on-screen. */
	XdgView& view = *magpie_container_of(listener, view, map);

	view.server.views.push_back(&view);
	view.server.focus_view(view, view.xdg_toplevel->base->surface);
}

static void xdg_toplevel_unmap_notify(wl_listener* listener, void* data) {
	(void) data;

	/* Called when the surface is unmapped, and should no longer be shown. */
	XdgView& view = *magpie_container_of(listener, view, unmap);

	/* Reset the cursor mode if the grabbed view was unmapped. */
	if (&view == view.server.grabbed_view) {
		view.server.seat->cursor->reset_mode();
	}

	view.server.views.remove(&view);
}

static void xdg_toplevel_destroy_notify(wl_listener* listener, void* data) {
	(void) data;

	/* Called when the surface is destroyed and should never be shown again. */
	XdgView& view = *magpie_container_of(listener, view, destroy);

	// just in case
	view.server.views.remove(&view);

	delete &view;
}

static void xdg_toplevel_request_move_notify(wl_listener* listener, void* data) {
	(void) data;

	/* This event is raised when a client would like to begin an interactive
	 * move, typically because the user clicked on their client-side
	 * decorations. Note that a more sophisticated compositor should check the
	 * provided serial against a list of button press serials sent to this
	 * client, to prevent the client from requesting this whenever they want. */
	XdgView& view = *magpie_container_of(listener, view, request_move);

	wlr_xdg_toplevel_set_maximized(view.xdg_toplevel, false);
	view.toplevel_handle->set_maximized(false);
	view.begin_interactive(MAGPIE_CURSOR_MOVE, 0);
}

static void xdg_toplevel_request_resize_notify(wl_listener* listener, void* data) {
	/* This event is raised when a client would like to begin an interactive
	 * resize, typically because the user clicked on their client-side
	 * decorations. Note that a more sophisticated compositor should check the
	 * provided serial against a list of button press serials sent to this
	 * client, to prevent the client from requesting this whenever they want. */
	XdgView& view = *magpie_container_of(listener, view, request_resize);

	struct wlr_xdg_toplevel_resize_event* event = static_cast<struct wlr_xdg_toplevel_resize_event*>(data);
	wlr_xdg_toplevel_set_maximized(view.xdg_toplevel, false);
	view.begin_interactive(MAGPIE_CURSOR_RESIZE, event->edges);
}

static void xdg_toplevel_request_maximize_notify(wl_listener* listener, void* data) {
	(void) data;

	/* This event is raised when a client would like to maximize itself,
	 * typically because the user clicked on the maximize button on
	 * client-side decorations. */
	XdgView& view = *magpie_container_of(listener, view, request_maximize);

	view.set_maximized(true);
}

static void xdg_toplevel_request_fullscreen_notify(wl_listener* listener, void* data) {
	(void) data;

	/* We must send a configure here, even on a no-op. */
	XdgView& view = *magpie_container_of(listener, view, request_fullscreen);

	wlr_xdg_surface_schedule_configure(view.xdg_toplevel->base);
}

static void xdg_toplevel_set_title_notify(wl_listener* listener, void* data) {
	(void) data;

	XdgView& view = *magpie_container_of(listener, view, set_title);

	view.toplevel_handle->set_title(view.xdg_toplevel->title);
}

static void xdg_toplevel_set_app_id_notify(wl_listener* listener, void* data) {
	(void) data;

	XdgView& view = *magpie_container_of(listener, view, set_app_id);

	view.toplevel_handle->set_app_id(view.xdg_toplevel->app_id);
}

static void xdg_toplevel_set_parent_notify(wl_listener* listener, void* data) {
	(void) data;

	XdgView& view = *magpie_container_of(listener, view, set_parent);

	if (view.xdg_toplevel->parent != nullptr) {
		magpie_surface_t* m_surface = static_cast<magpie_surface_t*>(view.xdg_toplevel->parent->base->data);
		if (m_surface != nullptr && m_surface->type == MAGPIE_SURFACE_TYPE_VIEW) {
			view.toplevel_handle->set_parent(m_surface->view->toplevel_handle);
			return;
		}
	}

	view.toplevel_handle->set_parent(nullptr);
}

XdgView::XdgView(Server& server, struct wlr_xdg_toplevel* toplevel) : server(server) {
	listeners.parent = this;

	auto* scene_tree = wlr_scene_xdg_surface_create(&server.scene->tree, toplevel->base);
	scene_node = &scene_tree->node;
	surface = toplevel->base->surface;

	wlr_xdg_surface_get_geometry(toplevel->base, &previous);
	wlr_xdg_toplevel_set_wm_capabilities(toplevel, WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE);

	magpie_surface_t* surface = new_magpie_surface_from_view(*this);
	scene_node->data = surface;
	toplevel->base->surface->data = surface;

	xdg_toplevel = toplevel;
	toplevel_handle = new ForeignToplevelHandle(*this);
	toplevel_handle->set_title(xdg_toplevel->title);
	toplevel_handle->set_app_id(xdg_toplevel->app_id);

	if (xdg_toplevel->parent != nullptr) {
		magpie_surface_t* m_surface = static_cast<magpie_surface_t*>(xdg_toplevel->parent->base->data);
		if (m_surface != nullptr && m_surface->type == MAGPIE_SURFACE_TYPE_VIEW) {
			toplevel_handle->set_parent(m_surface->view->toplevel_handle);
		}
	}

	/* Listen to the various events it can emit */
	listeners.map.notify = xdg_toplevel_map_notify;
	wl_signal_add(&toplevel->base->events.map, &listeners.map);
	listeners.unmap.notify = xdg_toplevel_unmap_notify;
	wl_signal_add(&toplevel->base->events.unmap, &listeners.unmap);
	listeners.destroy.notify = xdg_toplevel_destroy_notify;
	wl_signal_add(&toplevel->base->events.destroy, &listeners.destroy);

	/* cotd */
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
}

XdgView::~XdgView() noexcept {
	delete toplevel_handle;
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

Server& XdgView::get_server() {
	return server;
}

struct wlr_box XdgView::get_geometry() {
	wlr_box box;
	wlr_xdg_surface_get_geometry(xdg_toplevel->base, &box);
	return box;
}

void XdgView::set_size(int new_width, int new_height) {
	wlr_xdg_toplevel_set_size(xdg_toplevel, new_width, new_height);
}

void XdgView::begin_interactive(CursorMode mode, uint32_t edges) {
	Cursor* cursor = server.seat->cursor;
	struct wlr_surface* focused_surface = server.seat->wlr_seat->pointer_state.focused_surface;

	if (xdg_toplevel->base->surface != wlr_surface_get_root_surface(focused_surface)) {
		/* Deny move/resize requests from unfocused clients. */
		return;
	}

	server.grabbed_view = this;
	server.seat->cursor->mode = mode;

	if (mode == MAGPIE_CURSOR_MOVE) {
		server.grab_x = cursor->wlr_cursor->x - current.x;
		server.grab_y = cursor->wlr_cursor->y - current.y;
	} else {
		struct wlr_box geo_box;
		wlr_xdg_surface_get_geometry(xdg_toplevel->base, &geo_box);

		double border_x = (current.x + geo_box.x) + ((edges & WLR_EDGE_RIGHT) ? geo_box.width : 0);
		double border_y = (current.y + geo_box.y) + ((edges & WLR_EDGE_BOTTOM) ? geo_box.height : 0);
		server.grab_x = cursor->wlr_cursor->x - border_x;
		server.grab_y = cursor->wlr_cursor->y - border_y;

		server.grab_geobox = geo_box;
		server.grab_geobox.x += current.x;
		server.grab_geobox.y += current.y;

		server.resize_edges = edges;
	}
}

void XdgView::set_activated(bool activated) {
	wlr_xdg_toplevel_set_activated(xdg_toplevel, activated);
	toplevel_handle->set_activated(activated);
}

void XdgView::set_maximized(bool maximized) {
	Cursor& cursor = *server.seat->cursor;

	struct wlr_xdg_toplevel* toplevel = xdg_toplevel;
	if (toplevel->current.maximized == maximized) {
		/* Don't honor request if already maximized. */
		return;
	}

	struct wlr_surface* focused_surface = server.seat->wlr_seat->pointer_state.focused_surface;
	if (toplevel->base->surface != wlr_surface_get_root_surface(focused_surface)) {
		/* Deny maximize requests from unfocused clients. */
		return;
	}

	if (toplevel->current.maximized) {
		wlr_xdg_toplevel_set_size(toplevel, previous.width, previous.height);
		wlr_xdg_toplevel_set_maximized(toplevel, false);
		current.x = previous.x;
		current.y = previous.y;
		wlr_scene_node_set_position(scene_node, current.x, current.y);
	} else {
		wlr_xdg_surface_get_geometry(toplevel->base, &previous);
		previous.x = current.x;
		previous.y = current.y;

		Output* best_output = NULL;
		long best_area = 0;

		for (auto* output : server.outputs) {
			if (!wlr_output_layout_intersects(server.output_layout, output->wlr_output, &previous)) {
				continue;
			}

			struct wlr_box output_box;
			wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);
			struct wlr_box intersection;
			wlr_box_intersection(&intersection, &previous, &output_box);
			long intersection_area = intersection.width * intersection.height;

			if (intersection.width * intersection.height > best_area) {
				best_area = intersection_area;
				best_output = output;
			}
		}

		// if it's outside of all outputs, just use the pointer position
		if (best_output == NULL) {
			for (auto* output : server.outputs) {
				if (wlr_output_layout_contains_point(
						server.output_layout, output->wlr_output, cursor.wlr_cursor->x, cursor.wlr_cursor->y)) {
					best_output = output;
					break;
				}
			}
		}

		// still nothing? use the first output in the list
		if (best_output == NULL) {
			best_output = static_cast<Output*>(wlr_output_layout_get_center_output(server.output_layout)->data);
		}

		struct wlr_box output_box;
		wlr_output_layout_get_box(server.output_layout, best_output->wlr_output, &output_box);

		wlr_xdg_toplevel_set_size(toplevel, output_box.width, output_box.height);
		wlr_xdg_toplevel_set_maximized(toplevel, true);
		current.x = output_box.x;
		current.y = output_box.y;
		wlr_scene_node_set_position(scene_node, current.x, current.y);
	}
}
