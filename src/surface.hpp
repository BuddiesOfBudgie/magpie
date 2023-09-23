#ifndef MAGPIE_SURFACE_HPP
#define MAGPIE_SURFACE_HPP

#include "types.hpp"

#include <functional>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_scene.h>
#include "wlr-wrap-end.hpp"

enum SurfaceType { MAGPIE_SURFACE_TYPE_VIEW, MAGPIE_SURFACE_TYPE_LAYER, MAGPIE_SURFACE_TYPE_POPUP };

struct Surface {
	Server& server;

	const SurfaceType type;
	wlr_scene_node* scene_node;

	union {
		std::reference_wrapper<View> view;
		std::reference_wrapper<Layer> layer;
		std::reference_wrapper<Popup> popup;
	};

	Surface(View& view) noexcept;
	Surface(Layer& layer) noexcept;
	Surface(Popup& popup) noexcept;
};

#endif
