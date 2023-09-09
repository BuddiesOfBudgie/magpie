#ifndef MAGPIE_KEYBOARD_HPP
#define MAGPIE_KEYBOARD_HPP

#include "types.hpp"

#include <wayland-server-core.h>

class Keyboard {
  public:
	struct Listeners {
		Keyboard* parent;
		wl_listener modifiers;
		wl_listener key;
		wl_listener destroy;
	};

  private:
	Listeners listeners;

  public:
	Seat& seat;
	struct wlr_keyboard* wlr_keyboard;

	Keyboard(Seat& seat, struct wlr_keyboard* wlr_keyboard);
	~Keyboard() noexcept;
};

#endif
