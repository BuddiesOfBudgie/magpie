#include "constraint.hpp"

#include "seat.hpp"
#include "types.hpp"

#include <wayland-util.h>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_compositor.h>
#include "wlr-wrap-end.hpp"

static void constraint_set_region_notify(wl_listener* listener, void* data) {
	(void) listener;
	(void) data;
}

static void constraint_surface_commit_notify(wl_listener* listener, void* data) {
	(void) listener;
	(void) data;
}

static void constraint_destroy_notify(wl_listener* listener, void* data) {
	PointerConstraint& constraint = magpie_container_of(listener, constraint, destroy);
	(void) data;

	auto& current_constraint = constraint.seat.current_constraint;
	if (current_constraint.has_value() && current_constraint.value().get().wlr == constraint.wlr) {
		constraint.seat.cursor.warp_to_constraint(current_constraint.value());
		constraint.deactivate();
		current_constraint.reset();
	}

	delete &constraint;
}

PointerConstraint::PointerConstraint(Seat& seat, wlr_pointer_constraint_v1* constraint) noexcept
	: listeners(*this), seat(seat), wlr(constraint) {
	listeners.set_region.notify = constraint_set_region_notify;
	wl_signal_add(&constraint->events.set_region, &listeners.set_region);
	listeners.surface_commit.notify = constraint_surface_commit_notify;
	wl_signal_add(&constraint->surface->events.commit, &listeners.surface_commit);
	listeners.destroy.notify = constraint_destroy_notify;
	wl_signal_add(&constraint->events.destroy, &listeners.destroy);
}

PointerConstraint::~PointerConstraint() noexcept {
	wl_list_remove(&listeners.set_region.link);
	wl_list_remove(&listeners.surface_commit.link);
	wl_list_remove(&listeners.destroy.link);
}

void PointerConstraint::activate() const {
	wlr_pointer_constraint_v1_send_activated(wlr);
}

void PointerConstraint::deactivate() const {
	wlr_pointer_constraint_v1_send_deactivated(wlr);
}
