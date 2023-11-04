#include "view.hpp"

#include "foreign_toplevel.hpp"
#include "input/seat.hpp"
#include "output.hpp"
#include "server.hpp"

#include "types.hpp"
#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include "wlr-wrap-end.hpp"

const std::optional<const Output*> View::find_output_for_maximize() {
	Server& server = get_server();

	if (server.outputs.empty()) {
		return {};
	}

	Cursor& cursor = server.seat->cursor;
	Output* best_output = nullptr;
	long best_area = 0;

	for (auto* output : server.outputs) {
		if (!wlr_output_layout_intersects(server.output_layout, &output->wlr, &previous)) {
			continue;
		}

		wlr_box output_box;
		wlr_output_layout_get_box(server.output_layout, &output->wlr, &output_box);
		wlr_box intersection;
		wlr_box_intersection(&intersection, &previous, &output_box);
		long intersection_area = intersection.width * intersection.height;

		if (intersection.width * intersection.height > best_area) {
			best_area = intersection_area;
			best_output = output;
		}
	}

	// if it's outside of all outputs, just use the pointer position
	if (best_output == nullptr) {
		for (auto* output : server.outputs) {
			if (wlr_output_layout_contains_point(server.output_layout, &output->wlr, cursor.wlr.x, cursor.wlr.y)) {
				best_output = output;
				break;
			}
		}
	}

	// still nothing? use the first output in the list
	if (best_output == nullptr) {
		best_output = static_cast<Output*>(wlr_output_layout_get_center_output(server.output_layout)->data);
	}

	return best_output;
}

void View::begin_interactive(const CursorMode mode, const uint32_t edges) {
	Server& server = get_server();

	Cursor& cursor = server.seat->cursor;
	wlr_surface* focused_surface = server.seat->wlr->pointer_state.focused_surface;

	if (get_wlr_surface() != wlr_surface_get_root_surface(focused_surface)) {
		/* Deny move/resize requests from unfocused clients. */
		return;
	}

	server.grabbed_view = this;
	cursor.mode = mode;

	if (mode == MAGPIE_CURSOR_MOVE) {
		server.grab_x = cursor.wlr.x - current.x;
		server.grab_y = cursor.wlr.y - current.y;
	} else {
		wlr_box geo_box = get_geometry();

		double border_x = (current.x + geo_box.x) + ((edges & WLR_EDGE_RIGHT) ? geo_box.width : 0);
		double border_y = (current.y + geo_box.y) + ((edges & WLR_EDGE_BOTTOM) ? geo_box.height : 0);
		server.grab_x = cursor.wlr.x - border_x;
		server.grab_y = cursor.wlr.y - border_y;

		server.grab_geobox = geo_box;
		server.grab_geobox.x += current.x;
		server.grab_geobox.y += current.y;

		server.resize_edges = edges;
	}
}

void View::set_position(const int new_x, const int new_y) {
	if (curr_placement == VIEW_PLACEMENT_STACKING) {
		previous.x = current.x;
		previous.y = current.y;
	}
	current.x = new_x;
	current.y = new_y;
	wlr_scene_node_set_position(scene_node, new_x, new_y);
	impl_set_position(new_x, new_y);
}

void View::set_size(const int new_width, const int new_height) {
	if (curr_placement == VIEW_PLACEMENT_STACKING) {
		previous.width = current.width;
		previous.height = current.height;
	}
	current.width = new_width;
	current.height = new_height;
	impl_set_size(new_width, new_height);
}

void View::set_activated(const bool activated) {
	impl_set_activated(activated);

	if (toplevel_handle.has_value()) {
		toplevel_handle->set_activated(activated);
	}
}

void View::set_placement(const ViewPlacement new_placement, const bool force) {
	Server& server = get_server();

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
	set_size(previous.width, previous.height);
	impl_set_maximized(false);
	impl_set_fullscreen(false);
	set_position(previous.x, previous.y);
}

bool View::maximize() {
	auto best_output = find_output_for_maximize();
	if (!best_output.has_value()) {
		return false;
	}

	wlr_box output_box = best_output.value()->usable_area_in_layout_coords();
	set_size(output_box.width, output_box.height);
	impl_set_fullscreen(false);
	impl_set_maximized(true);
	set_position(output_box.x, output_box.y);

	return true;
}

bool View::fullscreen() {
	auto best_output = find_output_for_maximize();
	if (!best_output.has_value()) {
		return false;
	}

	wlr_box output_box = best_output.value()->full_area_in_layout_coords();
	set_size(output_box.width, output_box.height);
	impl_set_fullscreen(true);
	set_position(output_box.x, output_box.y);

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
		unmap();
		set_activated(false);
	} else {
		map();
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
