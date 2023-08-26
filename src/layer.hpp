#ifndef MAGPIE_LAYER_HPP
#define MAGPIE_LAYER_HPP

#include "types.hpp"

#include <wayland-server-core.h>

struct magpie_layer {
	Server* server;

	wl_list link;

	Output* output;
	struct wlr_layer_surface_v1* layer_surface;
	struct wlr_scene_layer_surface_v1* scene_layer_surface;

	wl_listener map;
	wl_listener unmap;
	wl_listener destroy;
	wl_listener commit;
	wl_listener new_popup;
	wl_listener new_subsurface;
	wl_listener output_destroy;

	wl_list subsurfaces;
};

struct magpie_layer_subsurface {
	magpie_layer_t* parent_layer;
	struct wlr_subsurface* wlr_subsurface;
	wl_list link;

	wl_listener map;
	wl_listener unmap;
	wl_listener destroy;
	wl_listener commit;
};

magpie_layer_t* new_magpie_layer(Server& server, struct wlr_layer_surface_v1* surface);

#endif
