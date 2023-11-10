#include "keyboard.hpp"

#include "seat.hpp"
#include "server.hpp"
#include "surface/view.hpp"

#include <algorithm>
#include <xkbcommon/xkbcommon.h>

#include "wlr-wrap-start.hpp"
#include <wlr/backend/multi.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_seat.h>
#include "wlr-wrap-end.hpp"

/* This event is raised by the keyboard base wlr_input_device to signal
 * the destruction of the wlr_keyboard. It will no longer receive events
 * and should be destroyed. */
static void keyboard_handle_destroy(wl_listener* listener, void* data) {
	Keyboard& keyboard = magpie_container_of(listener, keyboard, destroy);
	(void) data;

	std::vector<Keyboard*>& keyboards = keyboard.seat.keyboards;
	(void) std::ranges::remove(keyboards, &keyboard).begin();

	delete &keyboard;
}

static bool handle_compositor_keybinding(const Keyboard& keyboard, const uint32_t modifiers, const xkb_keysym_t sym) {
	Server& server = keyboard.seat.server;

	if (modifiers == WLR_MODIFIER_ALT) {
		switch (sym) {
			case XKB_KEY_Escape: {
				wl_display_terminate(server.display);
				return true;
			}
			case XKB_KEY_Tab: {
				/* Cycle to the next view */
				if (server.views.size() < 2) {
					return true;
				}
				View* next_view = *server.views.begin()++;
				server.focus_view(next_view);
				return true;
			}
			default: {
				break;
			}
		}
	} else if (sym >= XKB_KEY_XF86Switch_VT_1 && sym <= XKB_KEY_XF86Switch_VT_12) {
		if (wlr_backend_is_multi(keyboard.seat.server.backend)) {
			if (wlr_session* session = wlr_backend_get_session(keyboard.seat.server.backend)) {
				const unsigned vt = sym - XKB_KEY_XF86Switch_VT_1 + 1;
				wlr_session_change_vt(session, vt);
			}
		}
		return true;
	}

	return false;
}

/* This event is raised when a key is pressed or released. */
static void keyboard_handle_key(wl_listener* listener, void* data) {
	const Keyboard& keyboard = magpie_container_of(listener, keyboard, key);

	const auto* event = static_cast<wlr_keyboard_key_event*>(data);
	wlr_seat* seat = keyboard.seat.wlr;

	wlr_idle_notifier_v1_notify_activity(keyboard.seat.server.idle_notifier, seat);

	/* Translate libinput keycode -> xkbcommon */
	const uint32_t keycode = event->keycode + 8;
	/* Get a list of keysyms based on the keymap for this keyboard */
	const xkb_keysym_t* syms;
	const int nsyms = xkb_state_key_get_syms(keyboard.wlr.xkb_state, keycode, &syms);

	bool handled = false;
	const uint32_t modifiers = wlr_keyboard_get_modifiers(&keyboard.wlr);
	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		if (modifiers & WLR_MODIFIER_ALT) {
			/* If alt is held down and this button was _pressed_, we attempt to
			 * process it as a compositor keybinding. */
			for (int i = 0; i < nsyms; i++) {
				handled = handle_compositor_keybinding(keyboard, modifiers, syms[i]);
			}
		}
	}

	if (!handled) {
		/* Otherwise, we pass it along to the client. */
		wlr_seat_set_keyboard(seat, &keyboard.wlr);
		wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
	}
}

/* This event is raised when a modifier key, such as shift or alt, is
 * pressed. We simply communicate this to the client. */
static void keyboard_handle_modifiers(wl_listener* listener, void* data) {
	Keyboard& keyboard = magpie_container_of(listener, keyboard, modifiers);
	(void) data;

	/*
	 * A seat can only have one keyboard, but this is a limitation of the
	 * Wayland protocol - not wlroots. We assign all connected keyboards to the
	 * same seat. You can swap out the underlying wlr_keyboard like this and
	 * wlr_seat handles this transparently.
	 */
	wlr_seat_set_keyboard(keyboard.seat.wlr, &keyboard.wlr);
	/* Send modifiers to the client. */
	wlr_seat_keyboard_notify_modifiers(keyboard.seat.wlr, &keyboard.wlr.modifiers);
}

Keyboard::Keyboard(Seat& seat, wlr_keyboard& keyboard) noexcept : listeners(*this), seat(seat), wlr(keyboard) {
	/* We need to prepare an XKB keymap and assign it to the keyboard. This
	 * assumes the defaults (e.g. layout = "us"). */
	xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	xkb_keymap* keymap = xkb_keymap_new_from_names(context, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);

	wlr_keyboard_set_keymap(&keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	wlr_keyboard_set_repeat_info(&keyboard, 25, 600);

	/* Here we set up listeners for keyboard events. */
	listeners.modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&keyboard.events.modifiers, &listeners.modifiers);
	listeners.key.notify = keyboard_handle_key;
	wl_signal_add(&keyboard.events.key, &listeners.key);
	listeners.destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&keyboard.base.events.destroy, &listeners.destroy);

	wlr_seat_set_keyboard(seat.wlr, &keyboard);
}

Keyboard::~Keyboard() noexcept {
	wl_list_remove(&listeners.modifiers.link);
	wl_list_remove(&listeners.key.link);
	wl_list_remove(&listeners.destroy.link);
}
