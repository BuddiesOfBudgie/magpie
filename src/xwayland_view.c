#include "server.h"
#include "types.h"
#include "view.h"
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/util/edges.h>

static void xwayland_surface_map_notify(struct wl_listener* listener, void* data) {
	(void) data;

	/* Called when the surface is mapped, or ready to display on-screen. */
	magpie_xwayland_view_t* xwayland_view = wl_container_of(listener, xwayland_view, map);
	magpie_view_t* view = xwayland_view->base;

	magpie_surface_t* surface = new_magpie_surface_from_view(view);
	view->scene_tree->node.data = surface;
	view->surface = xwayland_view->xwayland_surface->surface;
	xwayland_view->xwayland_surface->surface->data = surface;

	wlr_scene_node_set_position(view->scene_node, view->current.x, view->current.y);

	wl_list_insert(&xwayland_view->base->server->views, &xwayland_view->base->link);
	focus_view(xwayland_view->base, xwayland_view->xwayland_surface->surface);
}

static void xwayland_surface_unmap_notify(struct wl_listener* listener, void* data) {
	(void) data;

	/* Called when the surface is unmapped, and should no longer be shown. */
	magpie_xwayland_view_t* xwayland_view = wl_container_of(listener, xwayland_view, unmap);
	magpie_server_t* server = xwayland_view->base->server;

	/* Reset the cursor mode if the grabbed view was unmapped. */
	if (xwayland_view->base == server->grabbed_view) {
		reset_cursor_mode(server);
	}

	if (server->seat->keyboard_state.focused_surface == xwayland_view->base->surface) {
		server->seat->keyboard_state.focused_surface = NULL;
	}

	wlr_scene_node_destroy(&xwayland_view->base->scene_tree->node);

	wl_list_remove(&xwayland_view->base->link);
}

static void xwayland_surface_destroy_notify(struct wl_listener* listener, void* data) {
	(void) data;

	/* Called when the surface is destroyed and should never be shown again. */
	magpie_xwayland_view_t* xwayland_view = wl_container_of(listener, xwayland_view, destroy);

	wl_list_remove(&xwayland_view->map.link);
	wl_list_remove(&xwayland_view->unmap.link);
	wl_list_remove(&xwayland_view->destroy.link);

	free(xwayland_view);
}

static void xwayland_surface_request_configure_notify(struct wl_listener* listener, void* data) {
	magpie_xwayland_view_t* xwayland_view = wl_container_of(listener, xwayland_view, request_configure);
	struct wlr_xwayland_surface* xwayland_surface = xwayland_view->xwayland_surface;
	struct wlr_xwayland_surface_configure_event* event = data;

	wlr_xwayland_surface_configure(xwayland_surface, event->x, event->y, event->width, event->height);
	xwayland_view->base->current = (struct wlr_box){event->x, event->y, event->width, event->height};

	if (xwayland_surface->mapped) {
		wlr_scene_node_set_position(&xwayland_view->base->scene_tree->node, event->x, event->y);
	}
}

static void xwayland_surface_set_geometry_notify(struct wl_listener* listener, void* data) {
	(void) data;

	magpie_xwayland_view_t* xwayland_view = wl_container_of(listener, xwayland_view, set_geometry);
	struct wlr_xwayland_surface* xwayland_surface = xwayland_view->xwayland_surface;

	xwayland_view->base->current =
		(struct wlr_box){xwayland_surface->x, xwayland_surface->y, xwayland_surface->width, xwayland_surface->height};
	if (xwayland_surface->mapped) {
		wlr_scene_node_set_position(
			&xwayland_view->base->scene_tree->node, xwayland_view->base->current.x, xwayland_view->base->current.y);
	}
}

static void begin_interactive(magpie_xwayland_view_t* xwayland_view, magpie_cursor_mode_t mode, uint32_t edges) {
	magpie_view_t* view = xwayland_view->base;
	magpie_server_t* server = view->server;
	struct wlr_surface* focused_surface = server->seat->pointer_state.focused_surface;

	if (xwayland_view->xwayland_surface->surface != wlr_surface_get_root_surface(focused_surface)) {
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
		geo_box.x = xwayland_view->xwayland_surface->x;
		geo_box.y = xwayland_view->xwayland_surface->y;
		geo_box.width = xwayland_view->xwayland_surface->width;
		geo_box.height = xwayland_view->xwayland_surface->height;

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

static void xwayland_surface_request_move_notify(struct wl_listener* listener, void* data) {
	(void) data;

	/* This event is raised when a client would like to begin an interactive
	 * move, typically because the user clicked on their client-side
	 * decorations. Note that a more sophisticated compositor should check the
	 * provided serial against a list of button press serials sent to this
	 * client, to prevent the client from requesting this whenever they want. */
	magpie_xwayland_view_t* xwayland_view = wl_container_of(listener, xwayland_view, request_move);
	wlr_xwayland_surface_set_maximized(xwayland_view->xwayland_surface, false);
	begin_interactive(xwayland_view, MAGPIE_CURSOR_MOVE, 0);
}

static void xwayland_surface_request_resize_notify(struct wl_listener* listener, void* data) {
	/* This event is raised when a client would like to begin an interactive
	 * resize, typically because the user clicked on their client-side
	 * decorations. Note that a more sophisticated compositor should check the
	 * provided serial against a list of button press serials sent to this
	 * client, to prevent the client from requesting this whenever they want. */
	struct wlr_xwayland_resize_event* event = data;
	magpie_xwayland_view_t* xwayland_view = wl_container_of(listener, xwayland_view, request_resize);
	wlr_xwayland_surface_set_maximized(xwayland_view->xwayland_surface, false);
	begin_interactive(xwayland_view, MAGPIE_CURSOR_RESIZE, event->edges);
}

magpie_view_t* new_magpie_xwayland_view(magpie_server_t* server, struct wlr_xwayland_surface* xwayland_surface) {
	magpie_view_t* view = calloc(1, sizeof(magpie_xwayland_view_t));
	view->server = server;

	magpie_xwayland_view_t* xwayland_view = calloc(1, sizeof(magpie_xwayland_view_t));
	xwayland_view->base = view;
	xwayland_view->xwayland_surface = xwayland_surface;

	view->xwayland_view = xwayland_view;
	view->type = MAGPIE_VIEW_TYPE_XWAYLAND;
	view->scene_tree = wlr_scene_tree_create(&server->scene->tree);
	view->scene_node = &view->scene_tree->node;

	/* Listen to the various events it can emit */
	xwayland_view->map.notify = xwayland_surface_map_notify;
	wl_signal_add(&xwayland_surface->events.map, &xwayland_view->map);
	xwayland_view->unmap.notify = xwayland_surface_unmap_notify;
	wl_signal_add(&xwayland_surface->events.unmap, &xwayland_view->unmap);
	xwayland_view->destroy.notify = xwayland_surface_destroy_notify;
	wl_signal_add(&xwayland_surface->events.destroy, &xwayland_view->destroy);
	xwayland_view->request_configure.notify = xwayland_surface_request_configure_notify;
	wl_signal_add(&xwayland_surface->events.request_configure, &xwayland_view->request_configure);
	xwayland_view->request_move.notify = xwayland_surface_request_move_notify;
	wl_signal_add(&xwayland_surface->events.request_move, &xwayland_view->request_move);
	xwayland_view->request_resize.notify = xwayland_surface_request_resize_notify;
	wl_signal_add(&xwayland_surface->events.request_resize, &xwayland_view->request_resize);
	xwayland_view->set_geometry.notify = xwayland_surface_set_geometry_notify;
	wl_signal_add(&xwayland_surface->events.set_geometry, &xwayland_view->set_geometry);

	return view;
}
