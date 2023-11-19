#include "output.hpp"

#include "server.hpp"
#include "surface/layer.hpp"
#include "types.hpp"

#include <set>
#include <utility>

#include <wlr-wrap-start.hpp>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr-wrap-end.hpp>

static void output_enable_notify(wl_listener* listener, void* data) {
	Output& output = magpie_container_of(listener, output, enable);
	(void) data;

	output.scene_output = wlr_scene_get_scene_output(output.server.scene, &output.wlr);
}

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

	if (output.scene_output == nullptr) {
		output.scene_output = wlr_scene_get_scene_output(output.server.scene, &output.wlr);
	}

	if (output.scene_output == nullptr || output.is_leased || !output.wlr.enabled) {
		return;
	}

	/* Render the scene if needed and commit the output */
	wlr_scene_output_commit(output.scene_output);

	timespec now = {};
	timespec_get(&now, TIME_UTC);
	wlr_scene_output_send_frame_done(output.scene_output, &now);
}

static void output_destroy_notify(wl_listener* listener, void* data) {
	Output& output = magpie_container_of(listener, output, destroy);
	(void) data;

	output.server.outputs.erase(&output);
	for (const auto* layer : std::as_const(output.layers)) {
		wlr_layer_surface_v1_destroy(&layer->layer_surface);
	}

	delete &output;
}

Output::Output(Server& server, wlr_output& wlr) noexcept : listeners(*this), server(server), wlr(wlr) {
	wlr.data = this;

	scene_output = wlr_scene_get_scene_output(server.scene, &wlr);

	listeners.enable.notify = output_enable_notify;
	wl_signal_add(&wlr.events.enable, &listeners.enable);
	listeners.mode.notify = output_mode_notify;
	wl_signal_add(&wlr.events.mode, &listeners.mode);
	listeners.frame.notify = output_frame_notify;
	wl_signal_add(&wlr.events.frame, &listeners.frame);
	listeners.destroy.notify = output_destroy_notify;
	wl_signal_add(&wlr.events.destroy, &listeners.destroy);
}

Output::~Output() noexcept {
	wl_list_remove(&listeners.mode.link);
	wl_list_remove(&listeners.frame.link);
	wl_list_remove(&listeners.destroy.link);
}

void Output::update_layout() {
	const wlr_scene_output* scene_output = wlr_scene_get_scene_output(server.scene, &wlr);

	full_area.x = scene_output->x;
	full_area.y = scene_output->y;
	wlr_output_effective_resolution(&wlr, &full_area.width, &full_area.height);

	usable_area = full_area;

	for (const auto* layer : std::as_const(layers)) {
		wlr_scene_layer_surface_v1_configure(layer->scene_layer_surface, &full_area, &usable_area);
	}
}
