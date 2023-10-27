#ifndef MAGPIE_CONSTRAINT_HPP
#define MAGPIE_CONSTRAINT_HPP

#include "types.hpp"

#include <functional>
#include <wayland-server-core.h>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include "wlr-wrap-end.hpp"

class PointerConstraint {
  public:
	struct Listeners {
		std::reference_wrapper<PointerConstraint> parent;
		wl_listener set_region;
		wl_listener surface_commit;
		wl_listener destroy;
		Listeners(PointerConstraint& parent) noexcept : parent(parent) {}
	};

  private:
	Listeners listeners;

  public:
	Seat& seat;
	wlr_pointer_constraint_v1* wlr;

	PointerConstraint(Seat& seat, wlr_pointer_constraint_v1* wlr) noexcept;
	~PointerConstraint() noexcept;

	void activate() const;
	void deactivate() const;
};

#endif
