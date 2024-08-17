#include "budgie_keyboard_shortcuts.hpp"

#include <budgie-keyboard-shortcuts-server-protocol.h>

static void budgie_keyboard_shortcuts_subscriber_destroy([[maybe_unused]] wl_client* client, wl_resource* resource) {
	auto* subscriber = static_cast<budgie_keyboard_shortcuts_subscriber*>(wl_resource_get_user_data(resource));

	wl_signal_emit_mutable(&subscriber->events.destroy, subscriber);

	wl_resource_set_user_data(resource, nullptr);
	wl_list_remove(&subscriber->link);
	wl_resource_destroy(resource);
}

static void budgie_keyboard_shortcuts_subscriber_register_shortcut(
	[[maybe_unused]] wl_client* client, wl_resource* resource, uint32_t modifiers, uint32_t keycode) {
	auto* subscriber = static_cast<budgie_keyboard_shortcuts_subscriber*>(wl_resource_get_user_data(resource));

	budgie_keyboard_shortcuts_shortcut* it;
	wl_list_for_each(it, &subscriber->registered_shortcuts, link) {
		if (it->modifiers == modifiers && it->keycode == keycode) {
			// already registered, can skip
			return;
		}
	}

	auto* shortcut = static_cast<budgie_keyboard_shortcuts_shortcut*>(calloc(1, sizeof(budgie_keyboard_shortcuts_shortcut)));
	if (shortcut == nullptr) {
		wl_client_post_no_memory(client);
		return;
	}

	shortcut->modifiers = modifiers;
	shortcut->keycode = keycode;
	wl_list_insert(&subscriber->registered_shortcuts, &shortcut->link);

	wl_signal_emit_mutable(&subscriber->events.register_shortcut, shortcut);
}

static void budgie_keyboard_shortcuts_subscriber_unregister_shortcut(
	[[maybe_unused]] wl_client* client, wl_resource* resource, uint32_t modifiers, uint32_t keycode) {
	auto* subscriber = static_cast<budgie_keyboard_shortcuts_subscriber*>(wl_resource_get_user_data(resource));

	budgie_keyboard_shortcuts_shortcut* target = nullptr;
	budgie_keyboard_shortcuts_shortcut* it;
	wl_list_for_each(it, &subscriber->registered_shortcuts, link) {
		if (it->modifiers == modifiers && it->keycode == keycode) {
			target = it;
			break;
		}
	}

	if (target == nullptr) {
		// the passed shortcut wasn't registered
		return;
	}

	wl_list_remove(&target->link);
	wl_signal_emit_mutable(&subscriber->events.unregister_shortcut, target);
	free(target);
}

static const struct budgie_keyboard_shortcuts_subscriber_interface budgie_keyboard_shortcuts_subscriber_impl = {
	.destroy = budgie_keyboard_shortcuts_subscriber_destroy,
	.register_shortcut = budgie_keyboard_shortcuts_subscriber_register_shortcut,
	.unregister_shortcut = budgie_keyboard_shortcuts_subscriber_unregister_shortcut,
};

static void budgie_keyboard_shortcuts_subscriber_destroy(wl_resource* resource) {
	budgie_keyboard_shortcuts_subscriber_destroy(nullptr, resource);
}



static void budgie_keyboard_shortcuts_manager_destroy([[maybe_unused]] wl_client* client, wl_resource* resource) {
	auto* manager = static_cast<budgie_keyboard_shortcuts_manager*>(wl_resource_get_user_data(resource));

	wl_signal_emit_mutable(&manager->events.destroy, manager);

	wl_resource_set_user_data(resource, nullptr);
	wl_resource_destroy(resource);
}

static void budgie_keyboard_shortcuts_manager_subscribe(wl_client* client, wl_resource* resource, uint32_t id) {
	auto* manager = static_cast<budgie_keyboard_shortcuts_manager*>(wl_resource_get_user_data(resource));

	auto* subscriber =
		static_cast<budgie_keyboard_shortcuts_subscriber*>(calloc(1, sizeof(budgie_keyboard_shortcuts_subscriber)));
	if (subscriber == nullptr) {
		wl_client_post_no_memory(client);
		return;
	}

	int32_t version = wl_resource_get_version(resource);
	auto* subscriber_resource = wl_resource_create(client, &budgie_keyboard_shortcuts_subscriber_interface, version, id);
	if (subscriber_resource == nullptr) {
		free(subscriber);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(subscriber_resource, &budgie_keyboard_shortcuts_subscriber_impl, subscriber,
		budgie_keyboard_shortcuts_subscriber_destroy);

	wl_list_insert(&manager->subscribers, &subscriber->link);

	wl_signal_emit_mutable(&manager->events.subscribe, subscriber);
}

static const struct budgie_keyboard_shortcuts_manager_interface budgie_keyboard_shortcuts_manager_impl = {
	.destroy = budgie_keyboard_shortcuts_manager_destroy,
	.subscribe = budgie_keyboard_shortcuts_manager_subscribe,
};

static void budgie_keyboard_shortcuts_manager_bind(wl_client* client, void* data, uint32_t version, uint32_t id) {
	auto* manager = static_cast<budgie_keyboard_shortcuts_manager*>(data);

	wl_resource* resource = wl_resource_create(client, &budgie_keyboard_shortcuts_manager_interface, (int32_t) version, id);
	if (resource == nullptr) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &budgie_keyboard_shortcuts_manager_impl, manager, nullptr);
}

budgie_keyboard_shortcuts_manager* budgie_keyboard_shortcuts_manager_create(wl_display* display, uint32_t version) {
	auto* ret = static_cast<budgie_keyboard_shortcuts_manager*>(calloc(1, sizeof(budgie_keyboard_shortcuts_manager)));

	ret->global = wl_global_create(display, &budgie_keyboard_shortcuts_manager_interface, (int32_t) version, nullptr,
		budgie_keyboard_shortcuts_manager_bind);

	if (ret->global == nullptr) {
		free(ret);
		return nullptr;
	}

	wl_list_init(&ret->subscribers);
	wl_signal_init(&ret->events.destroy);
	wl_signal_init(&ret->events.subscribe);

	return ret;
}
