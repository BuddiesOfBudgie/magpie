#ifndef MAGPIE_KEYBOARD_HPP
#define MAGPIE_KEYBOARD_HPP

#include "types.hpp"

#include <functional>
#include <memory>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_keyboard.h>
#include "wlr-wrap-end.hpp"

class Keyboard final : std::enable_shared_from_this<Keyboard> {
  public:
	struct Listeners {
		std::reference_wrapper<Keyboard> parent;
		wl_listener modifiers = {};
		wl_listener key = {};
		wl_listener destroy = {};
		explicit Listeners(Keyboard& parent) noexcept : parent(parent) {}
	};

  private:
	Listeners listeners;

  public:
	Seat& seat;
	wlr_keyboard& wlr;

	Keyboard(Seat& seat, wlr_keyboard& keyboard) noexcept;
	~Keyboard() noexcept;
};

#endif
