#ifndef MAGPIE_SEAT_HPP
#define MAGPIE_SEAT_HPP

#include "cursor.hpp"
#include "keyboard_shortcuts_subscriber.hpp"

#include <memory>
#include <optional>
#include <vector>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include "wlr-wrap-end.hpp"

class Seat final : public std::enable_shared_from_this<Seat> {
  public:
	struct Listeners {
		std::reference_wrapper<Seat> parent;
		wl_listener new_input = {};
		wl_listener new_virtual_pointer = {};
		wl_listener new_virtual_keyboard = {};
		wl_listener new_pointer_constraint = {};
		wl_listener request_cursor = {};
		wl_listener request_set_selection = {};
		wl_listener keyboard_shortcuts_subscribe = {};
		explicit Listeners(Seat& parent) noexcept : parent(parent) {}
	};

  private:
	Listeners listeners;

  public:
	Server& server;
	wlr_seat* wlr;
	Cursor cursor;
	std::vector<std::shared_ptr<Keyboard>> keyboards;

	wlr_virtual_pointer_manager_v1* virtual_pointer_mgr;
	wlr_virtual_keyboard_manager_v1* virtual_keyboard_mgr;
	wlr_pointer_constraints_v1* pointer_constraints;
	std::shared_ptr<PointerConstraint> current_constraint;

	budgie_keyboard_shortcuts_manager* keyboard_shortcuts_manager;
	std::vector<std::shared_ptr<KeyboardShortcutsSubscriber>> keyboard_shortcuts_subscribers;

	explicit Seat(Server& server) noexcept;

	void new_input_device(wlr_input_device* device);
	void set_constraint(wlr_pointer_constraint_v1* wlr_constraint);
	void apply_constraint(const wlr_pointer* pointer, double* dx, double* dy) const;
	bool is_pointer_locked(const wlr_pointer* pointer) const;
};

#endif
