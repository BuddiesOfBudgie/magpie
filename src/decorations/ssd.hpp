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
	wlr_box get_geometry() const;

	uint8_t get_vertical_offset() const;
	uint8_t get_horizontal_offset() const;
	int32_t get_extra_width() const;
	int32_t get_extra_height() const;
};

#endif
