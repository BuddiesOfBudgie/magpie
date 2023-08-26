#include "output.hpp"
#include "types.hpp"
#include <algorithm>
#include <cstdlib>

#include <set>
#include <wlr-wrap-start.hpp>
#include <wlr/types/wlr_scene.h>
#include <wlr-wrap-end.hpp>

static void output_frame_notify(wl_listener* listener, void* data) {
	(void) data;

	/* This function is called every time an output is ready to display a frame,
	 * generally at the output's refresh rate (e.g. 60Hz). */
	output_listener_container* container = wl_container_of(listener, container, frame);
	Output& output = *container->parent;

	struct wlr_scene* scene = output.server.scene;

	struct wlr_scene_output* scene_output = wlr_scene_get_scene_output(scene, output.wlr_output);

	/* Render the scene if needed and commit the output */
	wlr_scene_output_commit(scene_output);

	struct timespec now;
	timespec_get(&now, TIME_UTC);
	wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_destroy_notify(wl_listener* listener, void* data) {
	(void) data;

	output_listener_container* container = wl_container_of(listener, container, destroy);
	Output& output = *container->parent;

	wl_list_remove(&container->frame.link);
	wl_list_remove(&container->destroy.link);

	std::set<Output*>& outputs = output.server.outputs;
	outputs.erase(&output);
}

Output::Output(Server& server, struct wlr_output* wlr_output) : server(server) {
	listeners.parent = this;

	this->wlr_output = wlr_output;
	wlr_output->data = this;

	listeners.frame.notify = output_frame_notify;
	wl_signal_add(&wlr_output->events.frame, &listeners.frame);
	listeners.destroy.notify = output_destroy_notify;
	wl_signal_add(&wlr_output->events.destroy, &listeners.destroy);
}

void Output::update_areas() {
	struct wlr_scene_output* scene_output = wlr_scene_get_scene_output(server.scene, wlr_output);

	full_area.x = scene_output->x;
	full_area.y = scene_output->y;
	wlr_output_effective_resolution(wlr_output, &full_area.width, &full_area.height);

	usable_area = full_area;
}
