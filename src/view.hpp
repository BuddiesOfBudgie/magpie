#ifndef MAGPIE_VIEW_HPP
#define MAGPIE_VIEW_HPP

#include "types.hpp"

#include "wlr-wrap-start.hpp"
#include <wlr/xwayland.h>
#include <wlr/util/box.h>
#include "wlr-wrap-end.hpp"

typedef enum { MAGPIE_VIEW_TYPE_XDG, MAGPIE_VIEW_TYPE_XWAYLAND } magpie_view_type_t;

struct magpie_xdg_view {
	magpie_view_t* base;

	struct wlr_xdg_toplevel* xdg_toplevel;
	wl_listener map;
	wl_listener unmap;
	wl_listener destroy;
	wl_listener commit;
	wl_listener request_move;
	wl_listener request_resize;
	wl_listener request_maximize;
	wl_listener request_unmaximize;
	wl_listener request_fullscreen;
};

struct magpie_xwayland_view {
	magpie_view_t* base;

	struct wlr_xwayland_surface* xwayland_surface;
	wl_listener map;
	wl_listener unmap;
	wl_listener destroy;
	wl_listener commit;
	wl_listener request_configure;
	wl_listener request_move;
	wl_listener request_resize;
	wl_listener set_geometry;
};

struct magpie_view {
	magpie_server_t* server;

	wl_list link;

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
