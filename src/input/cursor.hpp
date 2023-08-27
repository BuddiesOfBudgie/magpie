#ifndef MAGPIE_CURSOR_HPP
#define MAGPIE_CURSOR_HPP

#include "types.hpp"

#include <wayland-server-core.h>

enum CursorMode { MAGPIE_CURSOR_PASSTHROUGH, MAGPIE_CURSOR_MOVE, MAGPIE_CURSOR_RESIZE };

struct cursor_listener_container {
	Cursor* parent;
	wl_listener motion;
	wl_listener motion_absolute;
	wl_listener button;
	wl_listener axis;
	wl_listener frame;
};

class Cursor {
  private:
	cursor_listener_container listeners;

	void process_move(uint32_t time);
	void process_resize(uint32_t time);

  public:
	Seat& seat;
	struct wlr_cursor* wlr_cursor;
	struct wlr_xcursor_manager* cursor_mgr;
	CursorMode mode;

	Cursor(Seat& seat);

	void attach_input_device(struct wlr_input_device* device);
	void process_motion(uint32_t time);
	void reset_mode();
};

#endif
