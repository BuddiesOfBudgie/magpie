#include "ssd.hpp"

#include "surface/view.hpp"
#include "server.hpp"

constexpr uint8_t TITLEBAR_HEIGHT = 24;
constexpr uint32_t TITLEBAR_COLOR = 0x303030;

static consteval std::array<float, 4> rrggbb_to_floats(uint32_t rrggbb) {
	return std::array<float, 4>(
		{(float) (rrggbb >> 16 & 0xff) / 255.0f, (float) (rrggbb >> 8 & 0xff) / 255.0f, (float) (rrggbb & 0xff) / 255.0f, 1.0});
}

Ssd::Ssd(View& parent) noexcept : view(parent) {
	scene_tree = wlr_scene_tree_create(wlr_scene_tree_from_node(parent.scene_node));
	wlr_scene_node_lower_to_bottom(&scene_tree->node);
	wlr_scene_node_set_position(&scene_tree->node, -1, -TITLEBAR_HEIGHT);
	wlr_scene_node_set_enabled(&scene_tree->node, true);

	auto color = rrggbb_to_floats(TITLEBAR_COLOR);
	titlebar_rect = wlr_scene_rect_create(scene_tree, parent.current.width + 2, TITLEBAR_HEIGHT, color.data());
	wlr_scene_node_set_enabled(&titlebar_rect->node, true);

	border_rect = wlr_scene_rect_create(scene_tree, parent.current.width + 2, parent.current.height + 1, color.data());
	wlr_scene_node_set_position(&border_rect->node, 0, TITLEBAR_HEIGHT);
	wlr_scene_node_set_enabled(&border_rect->node, true);
}

void Ssd::update() const {
	wlr_scene_rect_set_size(titlebar_rect, view.get_geometry().width + 2, TITLEBAR_HEIGHT);
	wlr_scene_rect_set_size(border_rect, view.get_geometry().width + 2, view.current.height + 1);
}

Ssd::~Ssd() {
	wlr_scene_node_destroy(&scene_tree->node);
}
