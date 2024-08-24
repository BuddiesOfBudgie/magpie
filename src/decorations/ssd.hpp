#ifndef MAGPIE_SSD_HPP
#define MAGPIE_SSD_HPP

#include "types.hpp"

#include <memory>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_scene.h>
#include "wlr-wrap-end.hpp"

class Ssd final : public std::enable_shared_from_this<Ssd> {
  public:
	View& view;
	wlr_scene_tree* scene_tree = nullptr;
	wlr_scene_rect* titlebar_rect = nullptr;
	wlr_scene_rect* border_rect = nullptr;

	Ssd(View& parent) noexcept;
	~Ssd();

	void update() const;
};

#endif
