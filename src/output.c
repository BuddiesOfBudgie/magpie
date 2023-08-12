#include "output.h"
#include "types.h"
#include <stdlib.h>

static void output_frame_notify(struct wl_listener* listener, void* data) {
    (void) data;

    /* This function is called every time an output is ready to display a frame,
     * generally at the output's refresh rate (e.g. 60Hz). */
    magpie_output_t* output = wl_container_of(listener, output, frame);
    struct wlr_scene* scene = output->server->scene;

    struct wlr_scene_output* scene_output = wlr_scene_get_scene_output(scene, output->wlr_output);

    /* Render the scene if needed and commit the output */
    wlr_scene_output_commit(scene_output);

    struct timespec now;
    timespec_get(&now, TIME_UTC);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_destroy_notify(struct wl_listener* listener, void* data) {
    (void) data;

    magpie_output_t* output = wl_container_of(listener, output, destroy);

    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);
    free(output);
}

magpie_output_t* new_magpie_output(magpie_server_t* server, struct wlr_output* wlr_output) {
    magpie_output_t* output = calloc(1, sizeof(magpie_output_t));
    wlr_output->data = output;
    output->wlr_output = wlr_output;
    output->server = server;

    output->frame.notify = output_frame_notify;
    wl_signal_add(&wlr_output->events.frame, &output->frame);
    output->destroy.notify = output_destroy_notify;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);

    return output;
}

void magpie_output_update_areas(magpie_output_t* output) {
    struct wlr_scene_output* scene_output =
        wlr_scene_get_scene_output(output->server->scene, output->wlr_output);

    output->full_area.x = scene_output->x;
    output->full_area.y = scene_output->y;
    wlr_output_effective_resolution(output->wlr_output, &output->full_area.width,
        &output->full_area.height);

    output->usable_area = output->full_area;
}
