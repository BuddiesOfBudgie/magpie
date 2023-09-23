#ifndef MAGPIE_KEYBOARD_HPP
#define MAGPIE_KEYBOARD_HPP

#include "types.hpp"

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_keyboard.h>
#include "wlr-wrap-end.hpp"

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
	wlr_keyboard* keyboard;

	Keyboard(Seat& seat, wlr_keyboard* keyboard) noexcept;
	~Keyboard() noexcept;
};

#endif
