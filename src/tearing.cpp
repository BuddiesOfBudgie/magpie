#include "tearing.hpp"

#include "server.hpp"

#include <surface/surface.hpp>

void tearing_manager_new_object_notify(wl_listener* listener, void* data) {
	TearingManager& tearing_manager = magpie_container_of(listener, tearing_manager, new_object);
	auto& tearing_object_wlr = *static_cast<wlr_tearing_control_v1*>(data);

	tearing_manager.tearing_objects.push_back(std::make_unique<TearingObject>(tearing_manager, tearing_object_wlr));
}

void tearing_object_set_hint_notify(wl_listener* listener, void*) {
	TearingObject& tearing_object = magpie_container_of(listener, tearing_object, set_hint);

	auto* surface = static_cast<Surface*>(tearing_object.wlr.surface->data);
	surface->tearing_hint = static_cast<wp_tearing_control_v1_presentation_hint>(tearing_object.wlr.hint);
}

void tearing_object_destroy_notify(wl_listener* listener, void*) {
	TearingObject& tearing_object = magpie_container_of(listener, tearing_object, destroy);
	std::erase_if(tearing_object.parent.tearing_objects, [&](const auto& other) { return other.get() == &tearing_object; });
}

TearingManager::TearingManager(Server& server) noexcept : listeners(*this), server(server) {
	wlr = wlr_tearing_control_manager_v1_create(server.display, 1);

	listeners.new_object.notify = tearing_manager_new_object_notify;
	wl_signal_add(&wlr->events.new_object, &listeners.new_object);
}

TearingObject::TearingObject(TearingManager& parent, wlr_tearing_control_v1& wlr) noexcept
	: listeners(*this), parent(parent), wlr(wlr) {
	listeners.destroy.notify = tearing_object_destroy_notify;
	wl_signal_add(&wlr.events.destroy, &listeners.destroy);
	listeners.set_hint.notify = tearing_object_set_hint_notify;
	wl_signal_add(&wlr.events.set_hint, &listeners.set_hint);
}
