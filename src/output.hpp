#ifndef MAGPIE_OUTPUT_HPP
#define MAGPIE_OUTPUT_HPP

#include "server.hpp"
#include "types.hpp"

#include "wlr-wrap-start.hpp"
#include <wlr/util/box.h>
#include "wlr-wrap-end.hpp"

struct output_listener_container {
	Output* parent;
	wl_listener frame;
	wl_listener destroy;
};

class Output {
  private:
	output_listener_container listeners;

  public:
	Server& server;
	struct wlr_output* wlr_output;
	struct wlr_box full_area;
	struct wlr_box usable_area;

	Output(Server& server, struct wlr_output* wlr_output);

	void update_areas();
};

#endif
