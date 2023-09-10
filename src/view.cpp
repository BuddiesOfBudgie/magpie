#include "view.hpp"

#include "foreign_toplevel.hpp"
#include "input/seat.hpp"
#include "output.hpp"
#include "server.hpp"

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/edges.h>
#include "wlr-wrap-end.hpp"

Output* View::find_output_for_maximize() {
	Server& server = get_server();

	if (server.outputs.empty()) {
		return nullptr;
	}

	Cursor& cursor = *server.seat->cursor;
	Output* best_output = NULL;
	long best_area = 0;

	for (auto* output : server.outputs) {
		if (!wlr_output_layout_intersects(server.output_layout, output->wlr_output, &previous)) {
			continue;
		}

		struct wlr_box output_box;
		wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);
		struct wlr_box intersection;
		wlr_box_intersection(&intersection, &previous, &output_box);
		long intersection_area = intersection.width * intersection.height;

		if (intersection.width * intersection.height > best_area) {
			best_area = intersection_area;
			best_output = output;
		}
	}

	// if it's outside of all outputs, just use the pointer position
	if (best_output == NULL) {
		for (auto* output : server.outputs) {
			if (wlr_output_layout_contains_point(
					server.output_layout, output->wlr_output, cursor.wlr_cursor->x, cursor.wlr_cursor->y)) {
				best_output = output;
				break;
			}
		}
	}

	// still nothing? use the first output in the list
	if (best_output == NULL) {
		best_output = static_cast<Output*>(wlr_output_layout_get_center_output(server.output_layout)->data);
	}

	return best_output;
}

void View::begin_interactive(CursorMode mode, uint32_t edges) {
	Server& server = get_server();

	Cursor* cursor = server.seat->cursor;
	struct wlr_surface* focused_surface = server.seat->wlr_seat->pointer_state.focused_surface;

	if (surface != wlr_surface_get_root_surface(focused_surface)) {
		/* Deny move/resize requests from unfocused clients. */
		return;
	}

	server.grabbed_view = this;
	server.seat->cursor->mode = mode;

	if (mode == MAGPIE_CURSOR_MOVE) {
		server.grab_x = cursor->wlr_cursor->x - current.x;
		server.grab_y = cursor->wlr_cursor->y - current.y;
	} else {
		struct wlr_box geo_box = get_geometry();

		double border_x = (current.x + geo_box.x) + ((edges & WLR_EDGE_RIGHT) ? geo_box.width : 0);
		double border_y = (current.y + geo_box.y) + ((edges & WLR_EDGE_BOTTOM) ? geo_box.height : 0);
		server.grab_x = cursor->wlr_cursor->x - border_x;
		server.grab_y = cursor->wlr_cursor->y - border_y;

		server.grab_geobox = geo_box;
		server.grab_geobox.x += current.x;
		server.grab_geobox.y += current.y;

		server.resize_edges = edges;
	}
}

void View::set_size(int new_width, int new_height) {
	impl_set_size(new_width, new_height);
}

void View::set_activated(bool activated) {
	impl_set_activated(activated);
	toplevel_handle->set_activated(activated);
}

void View::set_maximized(bool maximized) {
	Server& server = get_server();
	if (this->maximized == maximized) {
		/* Don't honor request if already maximized. */
		return;
	}

	struct wlr_surface* focused_surface = server.seat->wlr_seat->pointer_state.focused_surface;
	if (surface != wlr_surface_get_root_surface(focused_surface)) {
		/* Deny maximize requests from unfocused clients. */
		return;
	}

	if (this->maximized) {
		set_size(previous.width, previous.height);
		impl_set_maximized(false);
		current.x = previous.x;
		current.y = previous.y;
		wlr_scene_node_set_position(scene_node, current.x, current.y);
	} else {
		previous = get_geometry();
		previous.x = current.x;
		previous.y = current.y;

		Output* best_output = find_output_for_maximize();
		struct wlr_box output_box;
		wlr_output_layout_get_box(server.output_layout, best_output->wlr_output, &output_box);

		set_size(output_box.width, output_box.height);
		impl_set_maximized(true);
		current.x = output_box.x;
		current.y = output_box.y;
		wlr_scene_node_set_position(scene_node, current.x, current.y);
	}

	this->maximized = maximized;
	toplevel_handle->set_maximized(maximized);
}
