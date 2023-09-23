#ifndef MAGPIE_CURSOR_HPP
#define MAGPIE_CURSOR_HPP

#include "types.hpp"

#include <functional>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include "wlr-wrap-end.hpp"

enum CursorMode { MAGPIE_CURSOR_PASSTHROUGH, MAGPIE_CURSOR_MOVE, MAGPIE_CURSOR_RESIZE };

class Cursor {
  public:
	struct Listeners {
		std::reference_wrapper<Cursor> parent;
		wl_listener motion;
		wl_listener motion_absolute;
		wl_listener button;
		wl_listener axis;
		wl_listener frame;
		wl_listener new_constraint;
		Listeners(Cursor& parent) noexcept : parent(std::ref(parent)) {}
	};

  private:
	Listeners listeners;

	void process_move(uint32_t time);
	void process_resize(uint32_t time);

  public:
	const Seat& seat;
	CursorMode mode;
	wlr_cursor* cursor;
	wlr_xcursor_manager* cursor_mgr;

	Cursor(Seat& seat) noexcept;

	void attach_input_device(wlr_input_device* device);
	void process_motion(uint32_t time);
	void reset_mode();
};

#endif
