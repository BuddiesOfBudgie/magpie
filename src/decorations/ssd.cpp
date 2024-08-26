#include "ssd.hpp"

#include "surface/view.hpp"
#include "server.hpp"

constexpr uint8_t TITLEBAR_HEIGHT = 24;
constexpr uint32_t TITLEBAR_COLOR = 0x303030;
constexpr uint8_t BORDER_WIDTH = 1;
constexpr uint32_t BORDER_COLOR = 0x505050;

static consteval std::array<float, 4> rrggbb_to_floats(uint32_t rrggbb) {
	return std::array<float, 4>(
		{(float) (rrggbb >> 16 & 0xff) / 255.0f, (float) (rrggbb >> 8 & 0xff) / 255.0f, (float) (rrggbb & 0xff) / 255.0f, 1.0});
}

Ssd::Ssd(View& parent) noexcept : view(parent) {
	scene_tree = wlr_scene_tree_create(parent.scene_tree);
	wlr_scene_node_lower_to_bottom(&scene_tree->node);
	wlr_scene_node_set_position(&scene_tree->node, 0, 0);
	wlr_scene_node_set_enabled(&scene_tree->node, true);

	auto titlebar_color = rrggbb_to_floats(TITLEBAR_COLOR);
	auto view_geo = view.get_surface_geometry();
	titlebar_rect = wlr_scene_rect_create(scene_tree, view_geo.width, TITLEBAR_HEIGHT, titlebar_color.data());
	titlebar_rect->node.data = &parent;
	wlr_scene_node_set_position(&titlebar_rect->node, BORDER_WIDTH, BORDER_WIDTH);
	wlr_scene_node_lower_to_bottom(&titlebar_rect->node);
	wlr_scene_node_set_enabled(&titlebar_rect->node, true);

	auto border_color = rrggbb_to_floats(BORDER_COLOR);
	border_rect = wlr_scene_rect_create(
		scene_tree, view_geo.width + get_extra_width(), view_geo.height + get_extra_height(), border_color.data());
	wlr_scene_node_set_position(&border_rect->node, 0, 0);
	wlr_scene_node_lower_to_bottom(&border_rect->node);
	wlr_scene_node_set_enabled(&border_rect->node, true);
}

Ssd::~Ssd() {
	wlr_scene_node_destroy(&scene_tree->node);
}

void Ssd::update() const {
	auto view_geo = view.get_surface_geometry();
	wlr_scene_rect_set_size(titlebar_rect, view_geo.width, TITLEBAR_HEIGHT);
	wlr_scene_rect_set_size(border_rect, view_geo.width + get_extra_width(), view_geo.height + get_extra_height());
}

wlr_box Ssd::get_geometry() const {
	auto view_geo = view.get_surface_geometry();
	return {.x = view_geo.x - get_horizontal_offset(),
		.y = view_geo.y - get_vertical_offset(),
		.width = view_geo.width + get_extra_width(),
		.height = view_geo.height + get_extra_height()};
}

uint8_t Ssd::get_vertical_offset() const {
	return TITLEBAR_HEIGHT + BORDER_WIDTH;
}

uint8_t Ssd::get_horizontal_offset() const {
	return BORDER_WIDTH;
}

int32_t Ssd::get_extra_width() const {
	return BORDER_WIDTH * 2;
}

int32_t Ssd::get_extra_height() const {
	return TITLEBAR_HEIGHT + BORDER_WIDTH * 2;
}
