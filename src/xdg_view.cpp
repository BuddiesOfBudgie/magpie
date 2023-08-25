#include "input.hpp"
#include "output.hpp"
#include "server.hpp"
#include "surface.hpp"
#include "types.hpp"
#include "view.hpp"

#include <cstdlib>

#include "wlr-wrap-start.hpp"
#include <wlr/backend.h>
#include <wlr/util/edges.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "wlr-wrap-end.hpp"

static void xdg_toplevel_map_notify(struct wl_listener* listener, void* data) {
	(void) data;

	/* Called when the surface is mapped, or ready to display on-screen. */
	magpie_xdg_view_t* xdg_view = wl_container_of(listener, xdg_view, map);
	wl_list_insert(&xdg_view->base->server->views, &xdg_view->base->link);
	focus_view(xdg_view->base, xdg_view->xdg_toplevel->base->surface);
}

static void xdg_toplevel_unmap_notify(struct wl_listener* listener, void* data) {
	(void) data;

	/* Called when the surface is unmapped, and should no longer be shown. */
	magpie_xdg_view_t* xdg_view = wl_container_of(listener, xdg_view, unmap);

	/* Reset the cursor mode if the grabbed view was unmapped. */
	if (xdg_view->base == xdg_view->base->server->grabbed_view) {
		reset_cursor_mode(xdg_view->base->server);
	}

	wl_list_remove(&xdg_view->base->link);
}

static void xdg_toplevel_destroy_notify(struct wl_listener* listener, void* data) {
	(void) data;

	/* Called when the surface is destroyed and should never be shown again. */
	magpie_xdg_view_t* xdg_view = wl_container_of(listener, xdg_view, destroy);

	wl_list_remove(&xdg_view->map.link);
	wl_list_remove(&xdg_view->unmap.link);
	wl_list_remove(&xdg_view->destroy.link);
	wl_list_remove(&xdg_view->request_move.link);
	wl_list_remove(&xdg_view->request_resize.link);
	wl_list_remove(&xdg_view->request_maximize.link);
	wl_list_remove(&xdg_view->request_fullscreen.link);

	free(xdg_view);
}

static void begin_interactive(magpie_xdg_view_t* xdg_view, magpie_cursor_mode_t mode, uint32_t edges) {
	magpie_view_t* view = xdg_view->base;
	magpie_server_t* server = view->server;
	struct wlr_surface* focused_surface = server->seat->pointer_state.focused_surface;

	if (xdg_view->xdg_toplevel->base->surface != wlr_surface_get_root_surface(focused_surface)) {
		/* Deny move/resize requests from unfocused clients. */
		return;
	}

	server->grabbed_view = view;
	server->cursor_mode = mode;

	if (mode == MAGPIE_CURSOR_MOVE) {
		server->grab_x = server->cursor->x - view->current.x;
		server->grab_y = server->cursor->y - view->current.y;
	} else {
		struct wlr_box geo_box;
		wlr_xdg_surface_get_geometry(xdg_view->xdg_toplevel->base, &geo_box);

		double border_x = (view->current.x + geo_box.x) + ((edges & WLR_EDGE_RIGHT) ? geo_box.width : 0);
		double border_y = (view->current.y + geo_box.y) + ((edges & WLR_EDGE_BOTTOM) ? geo_box.height : 0);
		server->grab_x = server->cursor->x - border_x;
		server->grab_y = server->cursor->y - border_y;

		server->grab_geobox = geo_box;
		server->grab_geobox.x += view->current.x;
		server->grab_geobox.y += view->current.y;

		server->resize_edges = edges;
	}
}

static void xdg_toplevel_request_move_notify(struct wl_listener* listener, void* data) {
	(void) data;

	/* This event is raised when a client would like to begin an interactive
	 * move, typically because the user clicked on their client-side
	 * decorations. Note that a more sophisticated compositor should check the
	 * provided serial against a list of button press serials sent to this
	 * client, to prevent the client from requesting this whenever they want. */
	magpie_xdg_view_t* xdg_view = wl_container_of(listener, xdg_view, request_move);
	wlr_xdg_toplevel_set_maximized(xdg_view->xdg_toplevel, false);
	begin_interactive(xdg_view, MAGPIE_CURSOR_MOVE, 0);
}

static void xdg_toplevel_request_resize_notify(struct wl_listener* listener, void* data) {
	/* This event is raised when a client would like to begin an interactive
	 * resize, typically because the user clicked on their client-side
	 * decorations. Note that a more sophisticated compositor should check the
	 * provided serial against a list of button press serials sent to this
	 * client, to prevent the client from requesting this whenever they want. */
	struct wlr_xdg_toplevel_resize_event* event = static_cast<struct wlr_xdg_toplevel_resize_event*>(data);
	magpie_xdg_view_t* xdg_view = wl_container_of(listener, xdg_view, request_resize);
	wlr_xdg_toplevel_set_maximized(xdg_view->xdg_toplevel, false);
	begin_interactive(xdg_view, MAGPIE_CURSOR_RESIZE, event->edges);
}

static void xdg_toplevel_request_maximize_notify(struct wl_listener* listener, void* data) {
	(void) data;

	/* This event is raised when a client would like to maximize itself,
	 * typically because the user clicked on the maximize button on
	 * client-side decorations. */
	magpie_xdg_view_t* xdg_view = wl_container_of(listener, xdg_view, request_maximize);
	magpie_view_t* view = xdg_view->base;
	magpie_server_t* server = view->server;
	struct wlr_xdg_toplevel* toplevel = xdg_view->xdg_toplevel;
	struct wlr_surface* focused_surface = server->seat->pointer_state.focused_surface;
	if (toplevel->base->surface != wlr_surface_get_root_surface(focused_surface)) {
		/* Deny maximize requests from unfocused clients. */
		return;
	}

	if (toplevel->current.maximized) {
		wlr_xdg_toplevel_set_size(toplevel, view->previous.width, view->previous.height);
		wlr_xdg_toplevel_set_maximized(toplevel, false);
		view->current.x = view->previous.x;
		view->current.y = view->previous.y;
		wlr_scene_node_set_position(xdg_view->base->scene_node, view->current.x, view->current.y);
	} else {
		wlr_xdg_surface_get_geometry(toplevel->base, &view->previous);
		view->previous.x = view->current.x;
		view->previous.y = view->current.y;

		magpie_output_t* best_output = NULL;
		long best_area = 0;

		magpie_output_t* output;
		wl_list_for_each(output, &server->outputs, link) {
			if (!wlr_output_layout_intersects(server->output_layout, output->wlr_output, &view->previous)) {
				continue;
			}

			struct wlr_box output_box;
			wlr_output_layout_get_box(server->output_layout, output->wlr_output, &output_box);
			struct wlr_box intersection;
			wlr_box_intersection(&intersection, &view->previous, &output_box);
			long intersection_area = intersection.width * intersection.height;

			if (intersection.width * intersection.height > best_area) {
				best_area = intersection_area;
				best_output = output;
			}
		}

		// if it's outside of all outputs, just use the pointer position
		if (best_output == NULL) {
			wl_list_for_each(output, &server->outputs, link) {
				if (wlr_output_layout_contains_point(
						server->output_layout, output->wlr_output, server->cursor->x, server->cursor->y)) {
					best_output = output;
					break;
				}
			}
		}

		// still nothing? use the first output in the list
		if (best_output == NULL) {
			best_output = static_cast<magpie_output_t*>(wlr_output_layout_get_center_output(server->output_layout)->data);
		}

		struct wlr_box output_box;
		wlr_output_layout_get_box(server->output_layout, best_output->wlr_output, &output_box);

		wlr_xdg_toplevel_set_size(toplevel, output_box.width, output_box.height);
		wlr_xdg_toplevel_set_maximized(toplevel, true);
		view->current.x = output_box.x;
		view->current.y = output_box.y;
		wlr_scene_node_set_position(xdg_view->base->scene_node, view->current.x, view->current.y);
	}
}

static void xdg_toplevel_request_fullscreen_notify(struct wl_listener* listener, void* data) {
	(void) data;

	/* We must send a configure here, even on a no-op. */
	magpie_xdg_view_t* xdg_view = wl_container_of(listener, xdg_view, request_fullscreen);
	wlr_xdg_surface_schedule_configure(xdg_view->xdg_toplevel->base);
}

magpie_view_t* new_magpie_xdg_view(magpie_server_t* server, struct wlr_xdg_toplevel* toplevel) {
	magpie_view_t* view = (magpie_view_t*) std::calloc(1, sizeof(magpie_xdg_view_t));
	view->server = server;
	view->scene_tree = wlr_scene_xdg_surface_create(&server->scene->tree, toplevel->base);
	view->scene_node = &view->scene_tree->node;
	view->surface = toplevel->base->surface;

	wlr_xdg_surface_get_geometry(toplevel->base, &view->previous);
	wlr_xdg_toplevel_set_wm_capabilities(toplevel, WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE);

	magpie_surface_t* surface = new_magpie_surface_from_view(view);
	view->scene_node->data = surface;
	toplevel->base->surface->data = surface;

	magpie_xdg_view_t* xdg_view = (magpie_xdg_view_t*) std::calloc(1, sizeof(magpie_xdg_view_t));
	xdg_view->base = view;
	xdg_view->xdg_toplevel = toplevel;

	view->xdg_view = xdg_view;
	view->type = MAGPIE_VIEW_TYPE_XDG;

	/* Listen to the various events it can emit */
	xdg_view->map.notify = xdg_toplevel_map_notify;
	wl_signal_add(&toplevel->base->events.map, &xdg_view->map);
	xdg_view->unmap.notify = xdg_toplevel_unmap_notify;
	wl_signal_add(&toplevel->base->events.unmap, &xdg_view->unmap);
	xdg_view->destroy.notify = xdg_toplevel_destroy_notify;
	wl_signal_add(&toplevel->base->events.destroy, &xdg_view->destroy);

	/* cotd */
	xdg_view->request_move.notify = xdg_toplevel_request_move_notify;
	wl_signal_add(&xdg_view->xdg_toplevel->events.request_move, &xdg_view->request_move);
	xdg_view->request_resize.notify = xdg_toplevel_request_resize_notify;
	wl_signal_add(&xdg_view->xdg_toplevel->events.request_resize, &xdg_view->request_resize);
	xdg_view->request_maximize.notify = xdg_toplevel_request_maximize_notify;
	wl_signal_add(&xdg_view->xdg_toplevel->events.request_maximize, &xdg_view->request_maximize);
	xdg_view->request_fullscreen.notify = xdg_toplevel_request_fullscreen_notify;
	wl_signal_add(&xdg_view->xdg_toplevel->events.request_fullscreen, &xdg_view->request_fullscreen);

	return view;
}
