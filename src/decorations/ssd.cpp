#include "ssd.hpp"

#include "surface/view.hpp"
#include "server.hpp"

#include "wlr-wrap-start.hpp"
#include <wlr/util/log.h>
#include "wlr-wrap-end.hpp"

constexpr uint8_t TITLEBAR_HEIGHT = 24;
constexpr uint32_t TITLEBAR_ACTIVE_COLOR = 0x303030;
constexpr uint32_t TITLEBAR_INACTIVE_COLOR = 0x202020;
constexpr uint8_t BORDER_WIDTH = 1;
constexpr uint8_t EXTENTS_WIDTH = 12;
constexpr uint32_t BORDER_ACTIVE_COLOR = 0x505050;
constexpr uint32_t BORDER_INACTIVE_COLOR = 0x404040;

static wlr_box border_dimensions(const wlr_box& view_dimensions) {
	return {
		.x = EXTENTS_WIDTH,
		.y = EXTENTS_WIDTH,
		.width = view_dimensions.width + (BORDER_WIDTH * 2),
		.height = view_dimensions.height + TITLEBAR_HEIGHT + (BORDER_WIDTH * 2),
	};
}

static wlr_box extents_dimensions(const wlr_box& view_dimensions) {
	return {
		.x = 0,
		.y = 0,
		.width = view_dimensions.width + (EXTENTS_WIDTH * 2) + (BORDER_WIDTH * 2),
		.height = view_dimensions.height + TITLEBAR_HEIGHT + (EXTENTS_WIDTH * 2) + (BORDER_WIDTH * 2),
	};
}

static constexpr std::array<float, 4> rrggbb_to_floats(const uint32_t rrggbb) {
	return std::array<float, 4>(
		{(float) (rrggbb >> 16 & 0xff) / 255.0f, (float) (rrggbb >> 8 & 0xff) / 255.0f, (float) (rrggbb & 0xff) / 255.0f, 1.0});
}

Ssd::Ssd(View& parent) noexcept : view(parent) {
	scene_tree = wlr_scene_tree_create(parent.scene_tree);
	wlr_scene_node_lower_to_bottom(&scene_tree->node);
	wlr_scene_node_set_position(&scene_tree->node, 0, 0);
	wlr_scene_node_set_enabled(&scene_tree->node, true);

	constexpr auto titlebar_color = rrggbb_to_floats(TITLEBAR_INACTIVE_COLOR);
	auto view_geo = view.get_surface_geometry();
	titlebar_rect = wlr_scene_rect_create(scene_tree, view_geo.width, TITLEBAR_HEIGHT, titlebar_color.data());
	try {
		titlebar_rect->node.data = new SceneRectData{.type = SceneRectType::TITLEBAR, .parent = &parent};
	} catch ([[maybe_unused]] std::bad_alloc& ex) {
		wlr_log(WLR_ERROR, "Failed to allocate memory for window decoration titlebar");
		exit(EXIT_FAILURE);
	}
	wlr_scene_node_set_position(&titlebar_rect->node, BORDER_WIDTH + EXTENTS_WIDTH, BORDER_WIDTH + EXTENTS_WIDTH);
	wlr_scene_node_lower_to_bottom(&titlebar_rect->node);
	wlr_scene_node_set_enabled(&titlebar_rect->node, true);

	constexpr auto extents_color = std::array<float, 4>({0.0f, 0.0f, 0.0f, 0.0f});
	auto extents_box = extents_dimensions(view_geo);
	extents_rect = wlr_scene_rect_create(scene_tree, extents_box.width, extents_box.height, extents_color.data());
	try {
		extents_rect->node.data = new SceneRectData{.type = SceneRectType::EXTENTS, .parent = &parent};
	} catch ([[maybe_unused]] std::bad_alloc& ex) {
		wlr_log(WLR_ERROR, "Failed to allocate memory for window decoration extents");
		exit(EXIT_FAILURE);
	}
	wlr_scene_node_set_position(&extents_rect->node, extents_box.x, extents_box.y);
	wlr_scene_node_lower_to_bottom(&extents_rect->node);
	wlr_scene_node_set_enabled(&extents_rect->node, true);

	constexpr auto border_color = rrggbb_to_floats(BORDER_INACTIVE_COLOR);
	auto border_box = border_dimensions(view_geo);
	border_rect = wlr_scene_rect_create(scene_tree, border_box.width, border_box.height, border_color.data());
	try {
		border_rect->node.data = new SceneRectData{.type = SceneRectType::BORDER, .parent = &parent};
	} catch ([[maybe_unused]] std::bad_alloc& ex) {
		wlr_log(WLR_ERROR, "Failed to allocate memory for window decoration border");
		exit(EXIT_FAILURE);
	}
	wlr_scene_node_set_position(&border_rect->node, border_box.x, border_box.y);
	wlr_scene_node_lower_to_bottom(&border_rect->node);
	wlr_scene_node_set_enabled(&border_rect->node, true);
}

Ssd::~Ssd() {
	delete static_cast<SceneRectData*>(titlebar_rect->node.data);
	delete static_cast<SceneRectData*>(border_rect->node.data);
	delete static_cast<SceneRectData*>(extents_rect->node.data);
	wlr_scene_node_destroy(&scene_tree->node);
}

void Ssd::update() const {
	auto view_geo = view.surface_current;
	wlr_scene_rect_set_size(titlebar_rect, view_geo.width, TITLEBAR_HEIGHT);

	const auto border_box = border_dimensions(view_geo);
	wlr_scene_rect_set_size(border_rect, border_box.width, border_box.height);

	const auto extents_box = extents_dimensions(view_geo);
	wlr_scene_rect_set_size(extents_rect, extents_box.width, extents_box.height);
}

void Ssd::set_activated(const bool activated) const {
	auto titlebar_color = rrggbb_to_floats(activated ? TITLEBAR_ACTIVE_COLOR : TITLEBAR_INACTIVE_COLOR);
	wlr_scene_rect_set_color(titlebar_rect, titlebar_color.data());

	auto border_color = rrggbb_to_floats(activated ? BORDER_ACTIVE_COLOR : BORDER_INACTIVE_COLOR);
	wlr_scene_rect_set_color(border_rect, border_color.data());
}

wlr_box Ssd::get_geometry() const {
	auto view_geo = view.surface_current;
	return {.x = view_geo.x - get_horizontal_offset(),
		.y = view_geo.y - get_vertical_offset(),
		.width = view_geo.width + get_extra_width(),
		.height = view_geo.height + get_extra_height()};
}

wlr_box Ssd::get_extentless_geometry() const {
	auto view_geo = view.surface_current;
	return {.x = view_geo.x,
		.y = view_geo.y - TITLEBAR_HEIGHT,
		.width = view_geo.width,
		.height = view_geo.height + TITLEBAR_HEIGHT};
}

uint8_t Ssd::get_vertical_offset() const {
	return TITLEBAR_HEIGHT + BORDER_WIDTH + EXTENTS_WIDTH;
}

uint8_t Ssd::get_visual_vertical_offset() const {
	return TITLEBAR_HEIGHT + BORDER_WIDTH;
}

uint8_t Ssd::get_horizontal_offset() const {
	return BORDER_WIDTH + EXTENTS_WIDTH;
}

int32_t Ssd::get_extra_width() const {
	return (BORDER_WIDTH * 2) + (EXTENTS_WIDTH * 2);
}

int32_t Ssd::get_extra_height() const {
	return TITLEBAR_HEIGHT + (BORDER_WIDTH * 2) + (EXTENTS_WIDTH * 2);
}
