#ifndef MAGPIE_OUTPUT_HPP
#define MAGPIE_OUTPUT_HPP

#include "server.hpp"
#include "types.hpp"

#include "wlr-wrap-start.hpp"
#include <wlr/util/box.h>
#include "wlr-wrap-end.hpp"

class Output {
  public:
	struct Listeners {
		Output* parent;
		wl_listener mode;
		wl_listener frame;
		wl_listener destroy;
	};

  private:
	Listeners listeners;

  public:
	Server& server;
	struct wlr_output* wlr_output;
	struct wlr_box full_area;
	struct wlr_box usable_area;
	std::set<Layer*> layers;

	Output(Server& server, struct wlr_output* wlr_output);
	~Output() noexcept;

	void update_layout();
};

#endif
