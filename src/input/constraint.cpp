#include "constraint.hpp"

#include "seat.hpp"
#include "types.hpp"

#include <wayland-util.h>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_compositor.h>
#include "wlr-wrap-end.hpp"

static void constraint_destroy_notify(wl_listener* listener, void*) {
	PointerConstraint& constraint = magpie_container_of(listener, constraint, destroy);

	auto& current_constraint = constraint.seat.current_constraint;
	if (current_constraint.has_value() && &current_constraint.value().get().wlr == &constraint.wlr) {
		constraint.seat.cursor.warp_to_constraint(current_constraint.value());
		constraint.deactivate();
		current_constraint.reset();
	}

	delete &constraint;
}

PointerConstraint::PointerConstraint(Seat& seat, wlr_pointer_constraint_v1& wlr) noexcept
	: listeners(*this), seat(seat), wlr(wlr) {
	listeners.destroy.notify = constraint_destroy_notify;
	wl_signal_add(&wlr.events.destroy, &listeners.destroy);
}

PointerConstraint::~PointerConstraint() noexcept {
	wl_list_remove(&listeners.destroy.link);
}

void PointerConstraint::activate() const {
	wlr_pointer_constraint_v1_send_activated(&wlr);
}

void PointerConstraint::deactivate() const {
	wlr_pointer_constraint_v1_send_deactivated(&wlr);
}
