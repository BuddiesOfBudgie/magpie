#ifndef MAGPIE_CURSOR_HPP
#define MAGPIE_CURSOR_HPP

#include "types.hpp"

#include <functional>
#include <memory>
#include <string>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include "wlr-wrap-end.hpp"

enum CursorMode { MAGPIE_CURSOR_PASSTHROUGH, MAGPIE_CURSOR_MOVE, MAGPIE_CURSOR_RESIZE };

class Cursor final : std::enable_shared_from_this<Cursor> {
  public:
	struct Listeners {
		std::reference_wrapper<Cursor> parent;
		wl_listener motion = {};
		wl_listener motion_absolute = {};
		wl_listener button = {};
		wl_listener axis = {};
		wl_listener frame = {};
		wl_listener new_constraint = {};
		wl_listener gesture_pinch_begin = {};
		wl_listener gesture_pinch_update = {};
		wl_listener gesture_pinch_end = {};
		wl_listener gesture_swipe_begin = {};
		wl_listener gesture_swipe_update = {};
		wl_listener gesture_swipe_end = {};
		wl_listener gesture_hold_begin = {};
		wl_listener gesture_hold_end = {};
		wl_listener request_set_shape = {};
		explicit Listeners(Cursor& parent) noexcept : parent(parent) {}
	};

  private:
	Listeners listeners;

	void process_move(uint32_t time);
	void process_resize(uint32_t time) const;

  public:
	const Seat& seat;
	wlr_cursor& wlr;

	CursorMode mode;
	wlr_xcursor_manager* cursor_mgr;
	wlr_cursor_shape_manager_v1* shape_mgr;
	wlr_relative_pointer_manager_v1* relative_pointer_mgr;
	wlr_pointer_gestures_v1* pointer_gestures;
	std::string current_image;

	explicit Cursor(Seat& seat) noexcept;

	void attach_input_device(wlr_input_device* device) const;
	void process_motion(uint32_t time);
	void reset_mode();
	void warp_to_constraint(const PointerConstraint& constraint) const;
	void set_image(const std::string& name);
	void reload_image() const;
};

#endif
