#ifndef MAGPIE_LAYER_HPP
#define MAGPIE_LAYER_HPP

#include "types.hpp"

#include <wayland-server-core.h>

struct magpie_layer {
	magpie_server_t* server;

	struct wl_list link;

	magpie_output_t* output;
	struct wlr_layer_surface_v1* layer_surface;
	struct wlr_scene_layer_surface_v1* scene_layer_surface;

	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener commit;
	struct wl_listener new_popup;
	struct wl_listener new_subsurface;
	struct wl_listener output_destroy;

	struct wl_list subsurfaces;
};

struct magpie_layer_subsurface {
	magpie_layer_t* parent_layer;
	struct wlr_subsurface* wlr_subsurface;
	struct wl_list link;

	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener commit;
};

magpie_layer_t* new_magpie_layer(magpie_server_t* server, struct wlr_layer_surface_v1* surface);

#endif
