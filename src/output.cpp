#include "output.hpp"

#include "server.hpp"
#include "surface/layer.hpp"
#include "types.hpp"

#include <set>

#include <wlr-wrap-start.hpp>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr-wrap-end.hpp>

static void output_enable_notify(wl_listener* listener, void* data) {
	Output& output = magpie_container_of(listener, output, enable);
	(void) data;

	output.scene_output = wlr_scene_get_scene_output(output.server.scene, output.output);
}

/* This function is called every time an output is ready to display a frame,
 * generally at the output's refresh rate (e.g. 60Hz). */
static void output_mode_notify(wl_listener* listener, void* data) {
	Output& output = magpie_container_of(listener, output, mode);
	(void) data;

	output.update_layout();
}

/* This function is called every time an output is ready to display a frame,
 * generally at the output's refresh rate (e.g. 60Hz). */
static void output_frame_notify(wl_listener* listener, void* data) {
	Output& output = magpie_container_of(listener, output, frame);
	(void) data;

	if (output.scene_output == nullptr || output.is_leased || !output.output->enabled) {
		return;
	}

	wlr_scene* scene = output.server.scene;
	wlr_scene_output* scene_output = wlr_scene_get_scene_output(scene, output.output);

	/* Render the scene if needed and commit the output */
	wlr_scene_output_commit(scene_output);

	timespec now;
	timespec_get(&now, TIME_UTC);
	wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_destroy_notify(wl_listener* listener, void* data) {
	Output& output = magpie_container_of(listener, output, destroy);
	(void) data;

	output.server.outputs.erase(&output);
	for (auto* layer : output.layers) {
		wlr_layer_surface_v1_destroy(&layer->layer_surface);
	}

	delete &output;
}

Output::Output(Server& server, wlr_output* output) noexcept : listeners(*this), server(server) {
	this->output = output;
	output->data = this;

	is_leased = false;

	listeners.enable.notify = output_enable_notify;
	wl_signal_add(&output->events.enable, &listeners.enable);
	listeners.mode.notify = output_mode_notify;
	wl_signal_add(&output->events.mode, &listeners.mode);
	listeners.frame.notify = output_frame_notify;
	wl_signal_add(&output->events.frame, &listeners.frame);
	listeners.destroy.notify = output_destroy_notify;
	wl_signal_add(&output->events.destroy, &listeners.destroy);
}

Output::~Output() noexcept {
	wl_list_remove(&listeners.mode.link);
	wl_list_remove(&listeners.frame.link);
	wl_list_remove(&listeners.destroy.link);
}

void Output::update_layout() {
	wlr_scene_output* scene_output = wlr_scene_get_scene_output(server.scene, output);

	full_area.x = scene_output->x;
	full_area.y = scene_output->y;
	wlr_output_effective_resolution(output, &full_area.width, &full_area.height);

	usable_area = full_area;

	for (auto* layer : layers) {
		wlr_scene_layer_surface_v1_configure(layer->scene_layer_surface, &full_area, &usable_area);
	}
}

wlr_box Output::usable_area_in_layout_coords() const {
	double layout_x = 0, layout_y = 0;
	wlr_output_layout_output_coords(server.output_layout, output, &layout_x, &layout_y);

	wlr_box box = usable_area;
	box.x += layout_x;
	box.y += layout_y;
	return box;
}
