#ifndef MAGPIE_OUTPUT_HPP
#define MAGPIE_OUTPUT_HPP

#include "server.hpp"
#include "types.hpp"

#include "wlr-wrap-start.hpp"
#include <wlr/util/box.h>
#include "wlr-wrap-end.hpp"

struct magpie_output {
	Server* server;

	struct wlr_box full_area;
	struct wlr_box usable_area;

	wl_list link;
	struct wlr_output* wlr_output;
	wl_listener frame;
	wl_listener destroy;
};

magpie_output_t* new_magpie_output(Server& server, struct wlr_output* wlr_output);

void magpie_output_update_areas(magpie_output_t* output);

#endif
