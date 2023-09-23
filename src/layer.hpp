#ifndef MAGPIE_LAYER_HPP
#define MAGPIE_LAYER_HPP

#include "types.hpp"

#include <set>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include "wlr-wrap-end.hpp"

class Layer {
  public:
	struct Listeners {
		Layer* parent;
		wl_listener map;
		wl_listener unmap;
		wl_listener destroy;
		wl_listener commit;
		wl_listener new_popup;
		wl_listener new_subsurface;
	};

  private:
	Listeners listeners;

  public:
	Output& output;

	wlr_layer_surface_v1* layer_surface;
	wlr_scene_layer_surface_v1* scene_layer_surface;

	std::set<LayerSubsurface*> subsurfaces;

	Layer(Output& output, wlr_layer_surface_v1* surface) noexcept;
	~Layer() noexcept;
};

class LayerSubsurface {
  public:
	struct Listeners {
		LayerSubsurface* parent;
		wl_listener map;
		wl_listener unmap;
		wl_listener destroy;
		wl_listener commit;
	};

  private:
	Listeners listeners;

  public:
	Layer& parent;
	wlr_subsurface* subsurface;

	LayerSubsurface(Layer& parent_layer, wlr_subsurface* subsurface) noexcept;
	~LayerSubsurface() noexcept;
};

#endif
