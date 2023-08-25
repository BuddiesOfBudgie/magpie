#ifndef MAGPIE_VIEW_HPP
#define MAGPIE_VIEW_HPP

#include "types.hpp"

#include "wlr-wrap-start.hpp"
#include <wlr/xwayland.h>
#include "wlr-wrap-end.hpp"

typedef enum { MAGPIE_VIEW_TYPE_XDG, MAGPIE_VIEW_TYPE_XWAYLAND } magpie_view_type_t;

struct magpie_xdg_view {
	magpie_view_t* base;

	struct wlr_xdg_toplevel* xdg_toplevel;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener commit;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_unmaximize;
	struct wl_listener request_fullscreen;
};

struct magpie_xwayland_view {
	magpie_view_t* base;

	struct wlr_xwayland_surface* xwayland_surface;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener commit;
	struct wl_listener request_configure;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener set_geometry;
};

struct magpie_view {
	magpie_server_t* server;

	struct wl_list link;

	magpie_view_type_t type;
	struct wlr_box current;
	struct wlr_box pending;
	struct wlr_box previous;
	struct wlr_surface* surface;
	struct wlr_scene_tree* scene_tree;
	struct wlr_scene_node* scene_node;

	union {
		magpie_xdg_view_t* xdg_view;
		magpie_xwayland_view_t* xwayland_view;
	};
};

magpie_view_t* new_magpie_xdg_view(magpie_server_t* server, struct wlr_xdg_toplevel* toplevel);
magpie_view_t* new_magpie_xwayland_view(magpie_server_t* server, struct wlr_xwayland_surface* surface);

#endif
