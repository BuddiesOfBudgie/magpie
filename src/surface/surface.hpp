#ifndef MAGPIE_SURFACE_HPP
#define MAGPIE_SURFACE_HPP

#include "types.hpp"

#include <functional>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_scene.h>
#include "wlr-wrap-end.hpp"

enum SurfaceType { MAGPIE_SURFACE_TYPE_VIEW, MAGPIE_SURFACE_TYPE_LAYER, MAGPIE_SURFACE_TYPE_POPUP };

struct Surface {
	wlr_scene_node* scene_node;

	virtual ~Surface() noexcept {};

	virtual constexpr Server& get_server() const = 0;
	virtual bool is_view() const = 0;
};

#endif
