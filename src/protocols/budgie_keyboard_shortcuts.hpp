#ifndef MAGPIE_PROTOCOLS_BUDGIE_KEYBOARD_SHORTCUTS_HPP
#define MAGPIE_PROTOCOLS_BUDGIE_KEYBOARD_SHORTCUTS_HPP

#include <wayland-server-core.h>

struct budgie_keyboard_shortcuts_manager {
	wl_global* global;
	wl_list subscribers; // budgie_keyboard_shortcuts_subscriber.link

	struct {
		wl_signal destroy;	 // data: (budgie_keyboard_shortcuts_manager*)
		wl_signal subscribe; // data: (budgie_keyboard_shortcuts_subscriber*)
	} events;

	void* data;
};

budgie_keyboard_shortcuts_manager* budgie_keyboard_shortcuts_manager_create(wl_display* display, uint32_t version);

struct budgie_keyboard_shortcuts_shortcut {
	wl_list link;

	uint32_t modifiers;
	uint32_t keycode;
};

struct budgie_keyboard_shortcuts_subscriber {
	wl_resource* resource;
	wl_list link;
	wl_list registered_shortcuts; // budgie_keyboard_shortcuts_shortcut.link

	struct {
		wl_signal destroy;			   // data: (budgie_keyboard_shortcuts_subscriber*)
		wl_signal register_shortcut;   // data: (budgie_keyboard_shortcuts_shortcut*)
		wl_signal unregister_shortcut; // data: (budgie_keyboard_shortcuts_shortcut*)
	} events;

	void* data;
};

void budgie_keyboard_shortcuts_manager_send_shortcut_press(
	budgie_keyboard_shortcuts_subscriber* subscriber, uint32_t time_msec, uint32_t modifiers, uint32_t keycode);

#endif
