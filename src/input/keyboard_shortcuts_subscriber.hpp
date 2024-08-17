#ifndef MAGPIE_KEYBOARD_SHORTCUTS_SUBSCRIBER_HPP
#define MAGPIE_KEYBOARD_SHORTCUTS_SUBSCRIBER_HPP

#include "types.hpp"

#include "protocols/budgie_keyboard_shortcuts.hpp"

#include <memory>
#include <set>

class KeyboardShortcutsSubscriber final : public std::enable_shared_from_this<KeyboardShortcutsSubscriber> {
  public:
	struct Listeners {
		std::reference_wrapper<KeyboardShortcutsSubscriber> parent;
		wl_listener register_shortcut = {};
		wl_listener unregister_shortcut = {};
		wl_listener destroy = {};
		explicit Listeners(KeyboardShortcutsSubscriber& parent) noexcept : parent(parent) {}
	};

  private:
	Listeners listeners;

  public:
	Seat& seat;
	budgie_keyboard_shortcuts_subscriber& wlr;
	std::set<const budgie_keyboard_shortcuts_shortcut*> shortcuts;

	KeyboardShortcutsSubscriber(Seat& seat, budgie_keyboard_shortcuts_subscriber& subscriber) noexcept;
	~KeyboardShortcutsSubscriber() noexcept;
};

#endif
