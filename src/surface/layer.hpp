#ifndef MAGPIE_LAYER_HPP
#define MAGPIE_LAYER_HPP

#include "surface.hpp"
#include "types.hpp"
#include <set>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_subcompositor.h>
#include "wlr-wrap-end.hpp"

class Layer final : public Surface {
  public:
	struct Listeners {
		std::reference_wrapper<Layer> parent;
		wl_listener map = {};
		wl_listener unmap = {};
		wl_listener destroy = {};
		wl_listener commit = {};
		wl_listener new_popup = {};
		wl_listener new_subsurface = {};
		explicit Listeners(Layer& parent) noexcept : parent(parent) {}
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
	~Layer() noexcept override;

	[[nodiscard]] constexpr wlr_surface* get_wlr_surface() const override;
	[[nodiscard]] constexpr Server& get_server() const override;
	[[nodiscard]] constexpr bool is_view() const override;
};

class LayerSubsurface {
  public:
	struct Listeners {
		std::reference_wrapper<LayerSubsurface> parent;
		wl_listener map = {};
		wl_listener destroy = {};
		explicit Listeners(LayerSubsurface& parent) noexcept : parent(parent) {}
	};

  private:
	Listeners listeners;

  public:
	Layer& parent;
	wlr_subsurface& subsurface;

	LayerSubsurface(Layer& parent, wlr_subsurface& subsurface) noexcept;
	~LayerSubsurface() noexcept;
};

#endif
