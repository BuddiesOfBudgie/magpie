#ifndef MAGPIE_OUTPUT_H
#define MAGPIE_OUTPUT_H

#include "server.h"
#include "types.h"

struct magpie_output {
    magpie_server_t* server;

    struct wlr_box full_area;
    struct wlr_box usable_area;

    struct wl_list link;
    struct wlr_output* wlr_output;
    struct wl_listener frame;
    struct wl_listener destroy;
};

magpie_output_t* new_magpie_output(magpie_server_t* server, struct wlr_output* wlr_output);

void magpie_output_update_areas(magpie_output_t* output);

#endif
