#ifndef MAGPIE_INPUT_HPP
#define MAGPIE_INPUT_HPP

#include "types.hpp"

#include <wayland-server-core.h>

struct keyboard_listener_container {
	Keyboard* parent;
	wl_listener modifiers;
	wl_listener key;
	wl_listener destroy;
};

class Keyboard {
  private:
	keyboard_listener_container listeners;

  public:
	Server& server;
	struct wlr_keyboard* wlr_keyboard;

	Keyboard(Server& server, struct wlr_keyboard* wlr_keyboard);
};

typedef enum { MAGPIE_CURSOR_PASSTHROUGH, MAGPIE_CURSOR_MOVE, MAGPIE_CURSOR_RESIZE } magpie_cursor_mode_t;

void new_input_notify(wl_listener* listener, void* data);
void request_cursor_notify(wl_listener* listener, void* data);
void cursor_motion_notify(wl_listener* listener, void* data);
void cursor_motion_absolute_notify(wl_listener* listener, void* data);
void cursor_button_notify(wl_listener* listener, void* data);
void cursor_axis_notify(wl_listener* listener, void* data);
void cursor_frame_notify(wl_listener* listener, void* data);
void seat_request_set_selection(wl_listener* listener, void* data);
void reset_cursor_mode(Server& server);

#endif
