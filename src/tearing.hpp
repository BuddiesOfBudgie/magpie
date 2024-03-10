#ifndef MAGPIE_TEARING_HPP
#define MAGPIE_TEARING_HPP

#include "types.hpp"

#include <functional>
#include <list>
#include <memory>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_tearing_control_v1.h>
#include "wlr-wrap-end.hpp"

class TearingObject {
  public:
	struct Listeners {
		std::reference_wrapper<TearingObject> parent;
		wl_listener set_hint = {};
		wl_listener destroy = {};
		explicit Listeners(TearingObject& parent) noexcept : parent(parent) {}
	};

  private:
	Listeners listeners;

  public:
	TearingManager& parent;
	wlr_tearing_control_v1& wlr;

	explicit TearingObject(TearingManager& parent, wlr_tearing_control_v1& wlr) noexcept;
};

class TearingManager {
  public:
	struct Listeners {
		std::reference_wrapper<TearingManager> parent;
		wl_listener new_object = {};
		explicit Listeners(TearingManager& parent) noexcept : parent(parent) {}
	};

  private:
	Listeners listeners;

  public:
	Server& server;
	wlr_tearing_control_manager_v1* wlr;
	std::list<std::unique_ptr<TearingObject>> tearing_objects;

	explicit TearingManager(Server& server) noexcept;
};

#endif
