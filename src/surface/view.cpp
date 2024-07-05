#include "view.hpp"

#include <algorithm>
#include <utility>

#include "foreign_toplevel.hpp"
#include "input/seat.hpp"
#include "output.hpp"
#include "server.hpp"
#include "types.hpp"

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/edges.h>
#include "wlr-wrap-end.hpp"

std::optional<std::reference_wrapper<Output>> View::find_output_for_maximize() const {
	const Server& server = get_server();

	if (server.outputs.empty()) {
		return {};
	}

	const Cursor& cursor = server.seat->cursor;
	std::shared_ptr<Output> best_output = nullptr;
	int64_t best_area = 0;

	for (const auto& output : server.outputs) {
		if (!wlr_output_layout_intersects(server.output_layout, &output->wlr, &previous)) {
			continue;
		}

		wlr_box output_box = {};
		wlr_output_layout_get_box(server.output_layout, &output->wlr, &output_box);
		wlr_box intersection = {};
		wlr_box_intersection(&intersection, &previous, &output_box);

		auto intersection_width = static_cast<int64_t>(intersection.width);
		auto intersection_height = static_cast<int64_t>(intersection.height);
		const auto intersection_area = intersection_width * intersection_height;

		if (intersection_area > best_area) {
			best_area = intersection_area;
			best_output = output;
		}
	}

	// if it's outside all outputs, just use the pointer position
	if (best_output == nullptr) {
		for (const auto& output : server.outputs) {
			const auto cx = static_cast<int32_t>(std::round(cursor.wlr.x));
			const auto cy = static_cast<int32_t>(std::round(cursor.wlr.y));
			if (wlr_output_layout_contains_point(server.output_layout, &output->wlr, cx, cy)) {
				best_output = output;
				break;
			}
		}
	}

	// still nothing? use the first output in the list
	if (best_output == nullptr) {
		best_output = static_cast<Output*>(wlr_output_layout_get_center_output(server.output_layout)->data)->shared_from_this();
	}

	if (best_output == nullptr) {
		return {};
	}

	return std::ref(*best_output);
}

int32_t View::find_min_y() const {
	const Server& server = get_server();

	if (server.outputs.empty()) {
		return 0;
	}

	wlr_box current_copy = {.x = current.x, .y = 0, .width = current.width, .height = current.height + current.y};
	current_copy.height += current_copy.y;
	current_copy.y = 0;

	int32_t min_y = INT32_MAX;

	for (const auto& output : server.outputs) {
		wlr_box output_box = {};
		wlr_output_layout_get_box(server.output_layout, &output->wlr, &output_box);
		wlr_box intersection = {};
		wlr_box_intersection(&intersection, &previous, &output_box);

		if (!wlr_box_empty(&intersection)) {
			min_y = std::min(min_y, output_box.y);
		}
	}

	return min_y == INT32_MAX ? 0 : min_y;
}

void View::begin_interactive(const CursorMode mode, const uint32_t edges) {
	Server& server = get_server();

	Cursor& cursor = server.seat->cursor;
	wlr_surface* focused_surface = server.seat->wlr->pointer_state.focused_surface;

	if (get_wlr_surface() != wlr_surface_get_root_surface(focused_surface)) {
		/* Deny move/resize requests from unfocused clients. */
		return;
	}

	server.grabbed_view = std::dynamic_pointer_cast<View>(shared_from_this());
	cursor.mode = mode;

	if (mode == MAGPIE_CURSOR_MOVE) {
		server.grab_x = cursor.wlr.x - current.x;
		server.grab_y = cursor.wlr.y - current.y;
	} else {
		const wlr_box geo_box = get_geometry();

		const double border_x = current.x + geo_box.x + (((edges & WLR_EDGE_RIGHT) != 0) ? geo_box.width : 0);
		const double border_y = current.y + geo_box.y + (((edges & WLR_EDGE_BOTTOM) != 0) ? geo_box.height : 0);
		server.grab_x = cursor.wlr.x - border_x;
		server.grab_y = cursor.wlr.y - border_y;

		server.grab_geobox = geo_box;
		server.grab_geobox.x += current.x;
		server.grab_geobox.y += current.y;

		server.resize_edges = edges;
	}
}

void View::set_geometry(const int32_t x, const int32_t y, const int32_t width, const int32_t height) {
	const wlr_box min_size = get_min_size();
	const wlr_box max_size = get_max_size();
	const int32_t bounded_width = std::clamp(width, min_size.width, max_size.width);
	const int32_t bounded_height = std::clamp(height, min_size.height, max_size.height);

	if (curr_placement == VIEW_PLACEMENT_STACKING) {
		previous = current;
	}
	current = {.x = x, .y = y, .width = bounded_width, .height = bounded_height};
	current.y = std::max(y, find_min_y());
	if (scene_node != nullptr) {
		wlr_scene_node_set_position(scene_node, current.x, current.y);
	}
	impl_set_geometry(current.x, current.y, current.width, current.height);
}

void View::set_position(const int32_t x, const int32_t y) {
	if (curr_placement == VIEW_PLACEMENT_STACKING) {
		previous.x = current.x;
		previous.y = current.y;
	}
	current.x = x;
	current.y = y;
	current.y = std::max(y, find_min_y());
	if (scene_node != nullptr) {
		wlr_scene_node_set_position(scene_node, current.x, current.y);
	}
	impl_set_position(current.x, current.y);
}

void View::set_size(const int32_t width, const int32_t height) {
	const wlr_box min_size = get_min_size();
	const wlr_box max_size = get_max_size();
	const int32_t bounded_width = std::clamp(width, min_size.width, max_size.width);
	const int32_t bounded_height = std::clamp(height, min_size.height, max_size.height);

	if (curr_placement == VIEW_PLACEMENT_STACKING) {
		previous.width = current.width;
		previous.height = current.height;
	}
	current.width = bounded_width;
	current.height = bounded_height;
	impl_set_size(current.width, current.height);
}

void View::update_outputs(const bool ignore_previous) const {
	wlr_box largest_intersection = {};
	auto target_buffer_scale = 1.0;

	for (const auto& output : std::as_const(get_server().outputs)) {
		wlr_box output_area = output->full_area;

		wlr_box prev_intersect = {};
		wlr_box_intersection(&prev_intersect, &previous, &output_area);

		wlr_box curr_intersect = {};
		wlr_box_intersection(&curr_intersect, &current, &output_area);

		if (curr_intersect.width * curr_intersect.height > largest_intersection.width * largest_intersection.height) {
			largest_intersection = curr_intersect;
			target_buffer_scale = output->wlr.scale;
		}

		if (ignore_previous) {
			if (!wlr_box_empty(&curr_intersect)) {
				wlr_surface_send_enter(get_wlr_surface(), &output->wlr);
				toplevel_handle->output_enter(*output);
			}
		} else if (wlr_box_empty(&prev_intersect) && !wlr_box_empty(&curr_intersect)) {
			wlr_surface_send_enter(get_wlr_surface(), &output->wlr);
			toplevel_handle->output_enter(*output);
		} else if (!wlr_box_empty(&prev_intersect) && wlr_box_empty(&curr_intersect)) {
			wlr_surface_send_leave(get_wlr_surface(), &output->wlr);
			toplevel_handle->output_leave(*output);
		}
	}

	wlr_fractional_scale_v1_notify_scale(get_wlr_surface(), target_buffer_scale);
	wlr_surface_set_preferred_buffer_scale(get_wlr_surface(), std::ceil(target_buffer_scale));
}

void View::set_activated(const bool activated) {
	impl_set_activated(activated);

	if (toplevel_handle.has_value()) {
		toplevel_handle->set_activated(activated);
	}

	const auto seat = get_server().seat;
	if (activated) {
		wlr_scene_node_raise_to_top(scene_node);

		/*
		 * Tell the seat to have the keyboard enter this surface. wlroots will keep
		 * track of this and automatically send key events to the appropriate
		 * clients without additional work on your part.
		 */
		const auto* keyboard = wlr_seat_get_keyboard(seat->wlr);
		if (keyboard != nullptr) {
			wlr_seat_keyboard_notify_enter(
				seat->wlr, get_wlr_surface(), keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
		}

		wlr_pointer_constraint_v1* constraint =
			wlr_pointer_constraints_v1_constraint_for_surface(seat->pointer_constraints, get_wlr_surface(), seat->wlr);
		seat->set_constraint(constraint);
	} else {
		wlr_seat_keyboard_notify_clear_focus(seat->wlr);
		seat->set_constraint(nullptr);
	}
}

void View::set_placement(const ViewPlacement new_placement, const bool force) {
	const Server& server = get_server();

	if (!force) {
		if (curr_placement == new_placement) {
			return;
		}

		wlr_surface* focused_surface = server.seat->wlr->pointer_state.focused_surface;
		if (focused_surface == nullptr || get_wlr_surface() != wlr_surface_get_root_surface(focused_surface)) {
			/* Deny placement requests from unfocused clients. */
			return;
		}
	}

	bool res = true;

	switch (new_placement) {
		case VIEW_PLACEMENT_STACKING:
			stack();
			break;
		case VIEW_PLACEMENT_MAXIMIZED:
			res = maximize();
			break;
		case VIEW_PLACEMENT_FULLSCREEN:
			res = fullscreen();
			break;
	}

	if (res) {
		prev_placement = curr_placement;
		curr_placement = new_placement;
		if (toplevel_handle.has_value()) {
			toplevel_handle->set_placement(new_placement);
		}
	}
}

void View::stack() {
	impl_set_maximized(false);
	impl_set_fullscreen(false);
	set_geometry(previous.x, previous.y, previous.width, previous.height);
	update_outputs();
}

bool View::maximize() {
	const auto best_output = find_output_for_maximize();
	if (!best_output.has_value()) {
		return false;
	}

	const wlr_box output_box = best_output->get().usable_area;

	const wlr_box min_size = get_min_size();
	if (output_box.width < min_size.width || output_box.height < min_size.height) {
		return false;
	}

	const wlr_box max_size = get_max_size();
	if (output_box.width > max_size.width || output_box.height > max_size.height) {
		return false;
	}

	impl_set_fullscreen(false);
	impl_set_maximized(true);
	set_geometry(output_box.x, output_box.y, output_box.width, output_box.height);
	update_outputs();

	return true;
}

bool View::fullscreen() {
	const auto best_output = find_output_for_maximize();
	if (!best_output.has_value()) {
		return false;
	}

	const wlr_box output_box = best_output->get().full_area;

	const wlr_box min_size = get_min_size();
	if (output_box.width < min_size.width || output_box.height < min_size.height) {
		return false;
	}

	const wlr_box max_size = get_max_size();
	if (output_box.width > max_size.width || output_box.height > max_size.height) {
		return false;
	}

	impl_set_fullscreen(true);
	set_geometry(output_box.x, output_box.y, output_box.width, output_box.height);
	update_outputs();

	return true;
}

void View::set_minimized(const bool minimized) {
	if (minimized == is_minimized) {
		return;
	}

	if (toplevel_handle.has_value()) {
		toplevel_handle->set_minimized(minimized);
	}
	impl_set_minimized(minimized);
	this->is_minimized = minimized;

	if (minimized) {
		wlr_scene_node_set_enabled(scene_node, false);
		set_activated(false);
	} else {
		wlr_scene_node_set_enabled(scene_node, true);
	}
}

void View::toggle_maximize() {
	if (curr_placement != VIEW_PLACEMENT_FULLSCREEN) {
		set_placement(curr_placement != VIEW_PLACEMENT_MAXIMIZED ? VIEW_PLACEMENT_MAXIMIZED : VIEW_PLACEMENT_STACKING);
	}
}

void View::toggle_fullscreen() {
	if (curr_placement == VIEW_PLACEMENT_FULLSCREEN) {
		set_placement(prev_placement);
	} else {
		set_placement(VIEW_PLACEMENT_FULLSCREEN);
	}
}
