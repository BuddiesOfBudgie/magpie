#ifndef MAGPIE_KEYBOARD_HPP
#define MAGPIE_KEYBOARD_HPP

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
	Seat& seat;
	struct wlr_keyboard* wlr_keyboard;

	Keyboard(Seat& seat, struct wlr_keyboard* wlr_keyboard);
};

#endif
