#include "keyboard.hpp"

#include "seat.hpp"
#include "server.hpp"
#include "view.hpp"

#include <algorithm>
#include <xkbcommon/xkbcommon.h>

#include "wlr-wrap-start.hpp"
#include <wlr/backend/multi.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "wlr-wrap-end.hpp"

static void keyboard_handle_destroy(wl_listener* listener, void* data) {
	(void) data;

	/* This event is raised by the keyboard base wlr_input_device to signal
	 * the destruction of the wlr_keyboard. It will no longer receive events
	 * and should be destroyed.
	 */
	Keyboard& keyboard = *magpie_container_of(listener, keyboard, destroy);

	std::vector<Keyboard*>& keyboards = keyboard.seat.keyboards;
	std::remove(keyboards.begin(), keyboards.end(), &keyboard);

	delete &keyboard;
}

static bool handle_compositor_keybinding(Keyboard& keyboard, uint32_t modifiers, xkb_keysym_t sym) {
	Server& server = keyboard.seat.server;

	if (modifiers == WLR_MODIFIER_ALT) {
		switch (sym) {
			case XKB_KEY_Escape:
				wl_display_terminate(server.display);
				return true;
			case XKB_KEY_Tab:
				/* Cycle to the next view */
				if (server.views.size() < 2) {
					return true;
				}
				View& next_view = **server.views.begin()++;
				server.focus_view(next_view, next_view.surface);
				return true;
		}
	} else if (sym >= XKB_KEY_XF86Switch_VT_1 && sym <= XKB_KEY_XF86Switch_VT_12) {
		if (wlr_backend_is_multi(keyboard.seat.server.backend)) {
			wlr_session* session = wlr_backend_get_session(keyboard.seat.server.backend);
			if (session) {
				unsigned vt = sym - XKB_KEY_XF86Switch_VT_1 + 1;
				wlr_session_change_vt(session, vt);
			}
		}
		return true;
	}

	return false;
}

static void keyboard_handle_key(wl_listener* listener, void* data) {
	/* This event is raised when a key is pressed or released. */
	Keyboard& keyboard = *magpie_container_of(listener, keyboard, key);

	wlr_keyboard_key_event* event = static_cast<wlr_keyboard_key_event*>(data);
	wlr_seat* seat = keyboard.seat.wlr_seat;

	wlr_idle_notifier_v1_notify_activity(keyboard.seat.server.idle_notifier, seat);

	/* Translate libinput keycode -> xkbcommon */
	uint32_t keycode = event->keycode + 8;
	/* Get a list of keysyms based on the keymap for this keyboard */
	const xkb_keysym_t* syms;
	int nsyms = xkb_state_key_get_syms(keyboard.wlr_keyboard->xkb_state, keycode, &syms);

	bool handled = false;
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard.wlr_keyboard);
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
		wlr_seat_set_keyboard(seat, keyboard.wlr_keyboard);
		wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
	}
}

static void keyboard_handle_modifiers(wl_listener* listener, void* data) {
	(void) data;

	/* This event is raised when a modifier key, such as shift or alt, is
	 * pressed. We simply communicate this to the client. */
	Keyboard& keyboard = *magpie_container_of(listener, keyboard, modifiers);

	/*
	 * A seat can only have one keyboard, but this is a limitation of the
	 * Wayland protocol - not wlroots. We assign all connected keyboards to the
	 * same seat. You can swap out the underlying wlr_keyboard like this and
	 * wlr_seat handles this transparently.
	 */
	wlr_seat_set_keyboard(keyboard.seat.wlr_seat, keyboard.wlr_keyboard);
	/* Send modifiers to the client. */
	wlr_seat_keyboard_notify_modifiers(keyboard.seat.wlr_seat, &keyboard.wlr_keyboard->modifiers);
}

Keyboard::Keyboard(Seat& seat, struct wlr_keyboard* wlr_keyboard) : seat(seat) {
	listeners.parent = this;

	this->wlr_keyboard = wlr_keyboard;

	/* We need to prepare an XKB keymap and assign it to the keyboard. This
	 * assumes the defaults (e.g. layout = "us"). */
	xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	xkb_keymap* keymap = xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);

	wlr_keyboard_set_keymap(wlr_keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

	/* Here we set up listeners for keyboard events. */
	listeners.modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &listeners.modifiers);
	listeners.key.notify = keyboard_handle_key;
	wl_signal_add(&wlr_keyboard->events.key, &listeners.key);
	listeners.destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&wlr_keyboard->base.events.destroy, &listeners.destroy);

	wlr_seat_set_keyboard(seat.wlr_seat, wlr_keyboard);
}

Keyboard::~Keyboard() noexcept {
	wl_list_remove(&listeners.modifiers.link);
	wl_list_remove(&listeners.key.link);
	wl_list_remove(&listeners.destroy.link);
}
