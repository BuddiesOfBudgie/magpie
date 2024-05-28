#include "output.hpp"

#include "server.hpp"
#include "surface/layer.hpp"
#include "types.hpp"

#include <utility>

#include <wlr-wrap-start.hpp>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/log.h>
#include <wlr-wrap-end.hpp>

/* This function is called every time an output is ready to display a frame,
 * generally at the output's refresh rate (e.g. 60Hz). */
static void output_request_state_notify(wl_listener* listener, void* data) {
	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_output.events.request_state");
		return;
	}

	Output& output = magpie_container_of(listener, output, request_state);
	const auto* event = static_cast<wlr_output_event_request_state*>(data);

	wlr_output_commit_state(&output.wlr, event->state);
	output.update_layout();
}

/* This function is called every time an output is ready to display a frame,
 * generally at the output's refresh rate (e.g. 60Hz). */
static void output_frame_notify(wl_listener* listener, [[maybe_unused]] void* data) {
	Output& output = magpie_container_of(listener, output, frame);

	wlr_scene_output* scene_output = wlr_scene_get_scene_output(output.server.scene, &output.wlr);

	if (scene_output == nullptr || output.is_leased || !output.wlr.enabled) {
		return;
	}

	/* Render the scene if needed and commit the output */
	wlr_scene_output_commit(scene_output, nullptr);

	timespec now = {};
	timespec_get(&now, TIME_UTC);
	wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_destroy_notify(wl_listener* listener, [[maybe_unused]] void* data) {
	Output& output = magpie_container_of(listener, output, destroy);

	for (const auto& layer : output.layers) {
		wlr_layer_surface_v1_destroy(&layer->wlr);
	}
	output.server.outputs.erase(output.shared_from_this());
}

Output::Output(Server& server, wlr_output& wlr) noexcept : listeners(*this), server(server), wlr(wlr) {
	wlr.data = this;

	wlr_output_init_render(&wlr, server.allocator, server.renderer);

	wlr_output_state state = {};
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);

	wlr_output_mode* mode = wlr_output_preferred_mode(&wlr);
	if (mode != nullptr) {
		wlr_output_state_set_mode(&state, mode);
	}

	wlr_output_commit_state(&wlr, &state);
	wlr_output_state_finish(&state);

	listeners.request_state.notify = output_request_state_notify;
	wl_signal_add(&wlr.events.request_state, &listeners.request_state);
	listeners.frame.notify = output_frame_notify;
	wl_signal_add(&wlr.events.frame, &listeners.frame);
	listeners.destroy.notify = output_destroy_notify;
	wl_signal_add(&wlr.events.destroy, &listeners.destroy);

	wlr_output_layout_output* layout_output = wlr_output_layout_add_auto(server.output_layout, &wlr);
	wlr_scene_output* scene_output = wlr_scene_output_create(server.scene, &wlr);
	wlr_scene_output_layout_add_output(server.scene_layout, layout_output, scene_output);
}

Output::~Output() noexcept {
	wl_list_remove(&listeners.request_state.link);
	wl_list_remove(&listeners.frame.link);
	wl_list_remove(&listeners.destroy.link);
}

void Output::update_layout() {
	const wlr_scene_output* scene_output = wlr_scene_get_scene_output(server.scene, &wlr);
	if (scene_output == nullptr) {
		return;
	}

	full_area.x = scene_output->x;
	full_area.y = scene_output->y;
	wlr_output_effective_resolution(&wlr, &full_area.width, &full_area.height);

	usable_area = full_area;

	for (const auto& layer : layers) {
		wlr_scene_layer_surface_v1_configure(layer->scene_surface, &full_area, &usable_area);
	}
}
