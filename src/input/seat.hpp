#ifndef MAGPIE_SEAT_HPP
#define MAGPIE_SEAT_HPP

#include "cursor.hpp"
#include "constraint.hpp"
#include "types.hpp"

#include <optional>
#include <vector>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include "wlr-wrap-end.hpp"

class Seat {
  public:
	struct Listeners {
		std::reference_wrapper<Seat> parent;
		wl_listener new_input;
		wl_listener new_virtual_pointer;
		wl_listener new_virtual_keyboard;
		wl_listener new_pointer_constraint;
		wl_listener request_cursor;
		wl_listener request_set_selection;
		Listeners(Seat& parent) noexcept : parent(parent) {}
	};

  private:
	Listeners listeners;

  public:
	Server& server;
	wlr_seat* seat;
	Cursor cursor;
	std::vector<Keyboard*> keyboards;
	wlr_virtual_pointer_manager_v1* virtual_pointer_mgr;
	wlr_virtual_keyboard_manager_v1* virtual_keyboard_mgr;
	wlr_pointer_constraints_v1* pointer_constraints;
	std::optional<PointerConstraint> current_constraint = {};

	Seat(Server& server) noexcept;
	~Seat() noexcept;

	void new_input_device(wlr_input_device* device);
	void set_constraint(wlr_pointer_constraint_v1* wlr_constraint);
};

#endif
