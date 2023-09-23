#ifndef MAGPIE_OUTPUT_HPP
#define MAGPIE_OUTPUT_HPP

#include "types.hpp"

#include <functional>
#include <set>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include "wlr-wrap-end.hpp"

class Output {
  public:
	struct Listeners {
		std::reference_wrapper<Output> parent;
		wl_listener mode;
		wl_listener frame;
		wl_listener destroy;
		Listeners(Output& parent) noexcept : parent(parent) {}
	};

  private:
	Listeners listeners;

  public:
	Server& server;
	wlr_output* output;
	wlr_scene_output* scene_output;
	wlr_box full_area;
	wlr_box usable_area;
	std::set<Layer*> layers;
	bool is_leased;

	Output(Server& server, wlr_output* output) noexcept;
	~Output() noexcept;

	void update_layout();
};

#endif
