#ifndef MAGPIE_OUTPUT_HPP
#define MAGPIE_OUTPUT_HPP

#include "types.hpp"

#include <functional>
#include <memory>
#include <set>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include "wlr-wrap-end.hpp"

class Output final : public std::enable_shared_from_this<Output> {
  public:
	struct Listeners {
		std::reference_wrapper<Output> parent;
		wl_listener request_state = {};
		wl_listener frame = {};
		wl_listener destroy = {};
		explicit Listeners(Output& parent) noexcept : parent(parent) {}
	};

  private:
	Listeners listeners;

  public:
	Server& server;
	wlr_output& wlr;
	wlr_box full_area = {};
	wlr_box usable_area = {};
	std::set<std::shared_ptr<Layer>> layers;
	bool is_leased = false;

	Output(Server& server, wlr_output& wlr) noexcept;
	~Output() noexcept;

	void update_layout();
};

#endif
