#ifndef MAGPIE_LAYER_HPP
#define MAGPIE_LAYER_HPP

#include "types.hpp"

#include <set>
#include <wayland-server-core.h>

struct layer_listener_container {
	Layer* parent;
	wl_listener map;
	wl_listener unmap;
	wl_listener destroy;
	wl_listener commit;
	wl_listener new_popup;
	wl_listener new_subsurface;
	wl_listener output_destroy;
};

class Layer {
  private:
	layer_listener_container listeners;

  public:
	Server& server;
	Output* output;

	struct wlr_layer_surface_v1* layer_surface;
	struct wlr_scene_layer_surface_v1* scene_layer_surface;

	std::set<LayerSubsurface*> subsurfaces;

	Layer(Server& server, struct wlr_layer_surface_v1* surface);
};

struct layer_subsurface_listener_container {
	LayerSubsurface* parent;
	wl_listener map;
	wl_listener unmap;
	wl_listener destroy;
	wl_listener commit;
};

class LayerSubsurface {
  private:
	layer_subsurface_listener_container listeners;

  public:
	Layer& parent_layer;
	struct wlr_subsurface* wlr_subsurface;

	LayerSubsurface(Layer& parent_layer, struct wlr_subsurface* wlr_subsurface);
};

#endif
