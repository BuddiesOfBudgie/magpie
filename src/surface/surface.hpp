#ifndef MAGPIE_SURFACE_HPP
#define MAGPIE_SURFACE_HPP

#include "types.hpp"

#include <memory>
#include <set>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_scene.h>
#include "wlr-wrap-end.hpp"

enum SurfaceType { MAGPIE_SURFACE_TYPE_VIEW, MAGPIE_SURFACE_TYPE_LAYER, MAGPIE_SURFACE_TYPE_POPUP };

struct Surface : public virtual std::enable_shared_from_this<Surface> {
	wlr_scene_node* scene_node = nullptr;
	std::set<Popup*> popups;

	virtual ~Surface() noexcept = default;

	[[nodiscard]] virtual Server& get_server() const = 0;
	[[nodiscard]] virtual wlr_surface* get_wlr_surface() const = 0;
	[[nodiscard]] virtual bool is_view() const = 0;
};

#endif
