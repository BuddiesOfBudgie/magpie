#include "keyboard_shortcuts_subscriber.hpp"

#include "seat.hpp"

#include <algorithm>

#include "wlr-wrap-start.hpp"
#include <wlr/util/log.h>
#include "wlr-wrap-end.hpp"

void subscriber_destroy_notify(wl_listener* listener, void* data) {
	wlr_log(WLR_DEBUG, "budgie_keyboard_shortcuts_subscriber.events.destroy(listener=%p, data=%p)", (void*) listener, data);

	KeyboardShortcutsSubscriber& subscriber = magpie_container_of(listener, subscriber, destroy);

	auto& subscribers = subscriber.seat.keyboard_shortcuts_subscribers;
	(void) std::ranges::remove(subscribers, subscriber.shared_from_this());
}

void subscriber_register_shortcut_notify(wl_listener* listener, void* data) {
	wlr_log(WLR_DEBUG, "budgie_keyboard_shortcuts_subscriber.events.register_shortcut(listener=%p, data=%p)", (void*) listener,
		data);

	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to budgie_keyboard_shortcuts_subscriber.events.register_shortcut");
		return;
	}

	KeyboardShortcutsSubscriber& subscriber = magpie_container_of(listener, subscriber, register_shortcut);
	const auto* shortcut = static_cast<budgie_keyboard_shortcuts_shortcut*>(data);

	subscriber.shortcuts.insert(shortcut);
}

void subscriber_unregister_shortcut_notify(wl_listener* listener, void* data) {
	wlr_log(WLR_DEBUG, "budgie_keyboard_shortcuts_subscriber.events.unregister_shortcut(listener=%p, data=%p)",
		(void*) listener, data);

	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to budgie_keyboard_shortcuts_subscriber.events.unregister_shortcut");
		return;
	}

	KeyboardShortcutsSubscriber& subscriber = magpie_container_of(listener, subscriber, register_shortcut);
	const auto* shortcut = static_cast<budgie_keyboard_shortcuts_shortcut*>(data);

	subscriber.shortcuts.erase(shortcut);
}

KeyboardShortcutsSubscriber::KeyboardShortcutsSubscriber(Seat& seat, budgie_keyboard_shortcuts_subscriber& subscriber) noexcept
	: listeners(*this), seat(seat), wlr(subscriber) {
	listeners.destroy.notify = subscriber_destroy_notify;
	wl_signal_add(&wlr.events.destroy, &listeners.destroy);
	listeners.register_shortcut.notify = subscriber_register_shortcut_notify;
	wl_signal_add(&wlr.events.register_shortcut, &listeners.register_shortcut);
	listeners.unregister_shortcut.notify = subscriber_unregister_shortcut_notify;
	wl_signal_add(&wlr.events.unregister_shortcut, &listeners.unregister_shortcut);
}

KeyboardShortcutsSubscriber::~KeyboardShortcutsSubscriber() noexcept {
	wl_list_remove(&listeners.register_shortcut.link);
	wl_list_remove(&listeners.unregister_shortcut.link);
	wl_list_remove(&listeners.destroy.link);
}
