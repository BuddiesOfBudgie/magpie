#ifndef MAGPIE_SURFACE_HPP
#define MAGPIE_SURFACE_HPP

#include "types.hpp"

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_tearing_control_v1.h>
#include "wlr-wrap-end.hpp"

enum SurfaceType { MAGPIE_SURFACE_TYPE_VIEW, MAGPIE_SURFACE_TYPE_LAYER, MAGPIE_SURFACE_TYPE_POPUP };

struct Surface {
	wlr_scene_node* scene_node = nullptr;
	wp_tearing_control_v1_presentation_hint tearing_hint = WP_TEARING_CONTROL_V1_PRESENTATION_HINT_VSYNC;

	virtual ~Surface() noexcept = default;

	[[nodiscard]] virtual constexpr Server& get_server() const = 0;
	[[nodiscard]] virtual constexpr wlr_surface* get_wlr_surface() const = 0;
	[[nodiscard]] virtual constexpr bool is_view() const = 0;
};

#endif
