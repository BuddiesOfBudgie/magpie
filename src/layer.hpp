#ifndef MAGPIE_LAYER_HPP
#define MAGPIE_LAYER_HPP

#include "surface.hpp"
#include "types.hpp"

#include <functional>
#include <set>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include "wlr-wrap-end.hpp"

class Layer : public Surface {
  public:
	struct Listeners {
		std::reference_wrapper<Layer> parent;
		wl_listener map;
		wl_listener unmap;
		wl_listener destroy;
		wl_listener commit;
		wl_listener new_popup;
		wl_listener new_subsurface;
		Listeners(Layer& parent) noexcept : parent(parent) {}
	};

  private:
	Listeners listeners;

  public:
	Server& server;
	Output& output;

	wlr_layer_surface_v1& layer_surface;
	wlr_scene_layer_surface_v1* scene_layer_surface;

	std::set<LayerSubsurface*> subsurfaces;

	Layer(Output& output, wlr_layer_surface_v1& surface) noexcept;
	~Layer() noexcept;

	inline Server& get_server() const;
};

class LayerSubsurface {
  public:
	struct Listeners {
		std::reference_wrapper<LayerSubsurface> parent;
		wl_listener map;
		wl_listener unmap;
		wl_listener destroy;
		wl_listener commit;
		Listeners(LayerSubsurface& parent) noexcept : parent(parent) {}
	};

  private:
	Listeners listeners;

  public:
	Layer& parent;
	wlr_subsurface& subsurface;

	LayerSubsurface(Layer& parent_layer, wlr_subsurface& subsurface) noexcept;
	~LayerSubsurface() noexcept;
};

#endif
