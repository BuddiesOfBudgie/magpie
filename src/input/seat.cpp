#include "seat.hpp"

#include "constraint.hpp"
#include "keyboard.hpp"
#include "server.hpp"
#include "surface/view.hpp"
#include "types.hpp"

#include <algorithm>
#include <wayland-util.h>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include "wlr-wrap-end.hpp"

static void new_input_notify(wl_listener* listener, void* data) {
	wlr_log(WLR_DEBUG, "wlr_seat.events.new_input(listener=%p, data=%p)", (void*) listener, data);

	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_seat.events.new_input");
		return;
	}

	Seat& seat = magpie_container_of(listener, seat, new_input);
	auto* device = static_cast<wlr_input_device*>(data);

	seat.new_input_device(device);
}

static void new_virtual_pointer_notify(wl_listener* listener, void* data) {
	wlr_log(WLR_DEBUG, "wlr_seat.events.new_virtual_pointer(listener=%p, data=%p)", (void*) listener, data);

	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_seat.events.new_virtual_pointer");
		return;
	}

	Seat& seat = magpie_container_of(listener, seat, new_virtual_pointer);
	const auto* event = static_cast<wlr_virtual_pointer_v1_new_pointer_event*>(data);

	seat.new_input_device(&event->new_pointer->pointer.base);
}

static void new_virtual_keyboard_notify(wl_listener* listener, void* data) {
	wlr_log(WLR_DEBUG, "wlr_seat.events.new_virtual_keyboard(listener=%p, data=%p)", (void*) listener, data);

	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_seat.events.new_virtual_keyboard");
		return;
	}

	Seat& seat = magpie_container_of(listener, seat, new_virtual_keyboard);
	auto* keyboard = static_cast<wlr_virtual_keyboard_v1*>(data);

	seat.new_input_device(&keyboard->keyboard.base);
}

static void new_pointer_constraint_notify(wl_listener* listener, void* data) {
	wlr_log(WLR_DEBUG, "wlr_seat.events.new_pointer_constraint(listener=%p, data=%p)", (void*) listener, data);

	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_seat.events.new_pointer_constraint");
		return;
	}

	Seat& seat = magpie_container_of(listener, seat, new_pointer_constraint);
	auto* wlr_constraint = static_cast<wlr_pointer_constraint_v1*>(data);

	const auto* focused_surface = seat.wlr->keyboard_state.focused_surface;
	if (focused_surface == wlr_constraint->surface) {
		// only allow creating constraints for the focused view
		seat.set_constraint(wlr_constraint);
	}
}

static void request_cursor_notify(wl_listener* listener, void* data) {
	wlr_log(WLR_DEBUG, "wlr_seat.events.request_cursor(listener=%p, data=%p)", (void*) listener, data);

	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_seat.events.request_cursor");
		return;
	}

	const Seat& seat = magpie_container_of(listener, seat, request_cursor);
	const auto* event = static_cast<wlr_seat_pointer_request_set_cursor_event*>(data);

	const wlr_seat_client* focused_client = seat.wlr->pointer_state.focused_client;

	if (focused_client == event->seat_client) {
		/* Once we've vetted the client, we can tell the cursor to use the
		 * provided surface as the cursor image. It will set the hardware cursor
		 * on the output that it's currently on and continue to do so as the
		 * cursor moves between outputs. */
		wlr_cursor_set_surface(&seat.cursor.wlr, event->surface, event->hotspot_x, event->hotspot_y);
	}
}

/* This event is raised by the seat when a client wants to set the selection,
 * usually when the user copies something. wlroots allows compositors to
 * ignore such requests if they so choose, but in magpie we always honor
 */
static void request_set_selection_notify(wl_listener* listener, void* data) {
	wlr_log(WLR_DEBUG, "wlr_seat.events.set_selection(listener=%p, data=%p)", (void*) listener, data);

	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_seat.events.request_set_selection");
		return;
	}

	const Seat& seat = magpie_container_of(listener, seat, request_set_selection);
	const auto* event = static_cast<wlr_seat_request_set_selection_event*>(data);

	wlr_seat_set_selection(seat.wlr, event->source, event->serial);
}

static void keyboard_shortcuts_subscribe_notify(wl_listener* listener, void* data) {
	wlr_log(WLR_DEBUG, "budgie_keyboard_shortcuts_manager.events.subscribe(listener=%p, data=%p)", (void*) listener, data);

	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to budgie_keyboard_shortcuts_manager.events.subscribe");
		return;
	}

	Seat& seat = magpie_container_of(listener, seat, keyboard_shortcuts_subscribe);
	auto* subscriber = static_cast<budgie_keyboard_shortcuts_subscriber*>(data);

	seat.keyboard_shortcuts_subscribers.push_back(std::make_shared<KeyboardShortcutsSubscriber>(seat, *subscriber));
}

/*
 * Configures a seat, which is a single "seat" at which a user sits and
 * operates the computer. This conceptually includes up to one keyboard,
 * pointer, touch, and drawing tablet device. We also rig up a listener to
 * let us know when new input devices are available on the backend.
 */
Seat::Seat(Server& server) noexcept : listeners(*this), server(server), cursor(*this) {
	wlr = wlr_seat_create(server.display, "seat0");

	listeners.new_input.notify = new_input_notify;
	wl_signal_add(&server.backend->events.new_input, &listeners.new_input);
	listeners.request_cursor.notify = request_cursor_notify;
	wl_signal_add(&wlr->events.request_set_cursor, &listeners.request_cursor);
	listeners.request_set_selection.notify = request_set_selection_notify;
	wl_signal_add(&wlr->events.request_set_selection, &listeners.request_set_selection);

	virtual_pointer_mgr = wlr_virtual_pointer_manager_v1_create(server.display);
	listeners.new_virtual_pointer.notify = new_virtual_pointer_notify;
	wl_signal_add(&virtual_pointer_mgr->events.new_virtual_pointer, &listeners.new_virtual_pointer);

	virtual_keyboard_mgr = wlr_virtual_keyboard_manager_v1_create(server.display);
	listeners.new_virtual_keyboard.notify = new_virtual_keyboard_notify;
	wl_signal_add(&virtual_keyboard_mgr->events.new_virtual_keyboard, &listeners.new_virtual_keyboard);

	pointer_constraints = wlr_pointer_constraints_v1_create(server.display);
	listeners.new_pointer_constraint.notify = new_pointer_constraint_notify;
	wl_signal_add(&pointer_constraints->events.new_constraint, &listeners.new_pointer_constraint);

	keyboard_shortcuts_manager = budgie_keyboard_shortcuts_manager_create(server.display, 1);
	listeners.keyboard_shortcuts_subscribe.notify = keyboard_shortcuts_subscribe_notify;
	wl_signal_add(&keyboard_shortcuts_manager->events.subscribe, &listeners.keyboard_shortcuts_subscribe);
}

void Seat::new_input_device(wlr_input_device* device) {
	switch (device->type) {
		case WLR_INPUT_DEVICE_KEYBOARD:
			keyboards.push_back(std::make_shared<Keyboard>(*this, *wlr_keyboard_from_input_device(device)));
			break;
		case WLR_INPUT_DEVICE_POINTER:
		case WLR_INPUT_DEVICE_TABLET:
		case WLR_INPUT_DEVICE_TABLET_PAD:
		case WLR_INPUT_DEVICE_TOUCH:
			cursor.attach_input_device(device);
			break;
		default:
			break;
	}

	uint32_t caps = WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_TOUCH;
	if (!keyboards.empty()) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(wlr, caps);
}

void Seat::set_constraint(wlr_pointer_constraint_v1* wlr_constraint) {
	if (current_constraint != nullptr) {
		if (&current_constraint->wlr == wlr_constraint) {
			// we already have this constraint marked as the current constraint
			return;
		}

		cursor.warp_to_constraint(*current_constraint);
		current_constraint.reset();
	}

	if (wlr_constraint != nullptr) {
		current_constraint = std::make_shared<PointerConstraint>(*this, *wlr_constraint);
		current_constraint->activate();
	}
}

void Seat::apply_constraint(const wlr_pointer* pointer, double* dx, double* dy) const {
	if (current_constraint == nullptr || pointer->base.type != WLR_INPUT_DEVICE_POINTER) {
		return;
	}

	auto focused_view = server.focused_view.lock();

	if (focused_view == nullptr) {
		wlr_log(WLR_DEBUG, "Attempted to apply a pointer constraint without a focused view");
		return;
	}

	double x = cursor.wlr.x;
	double y = cursor.wlr.y;

	x -= focused_view->current.x;
	y -= focused_view->current.y;

	double confined_x = 0;
	double confined_y = 0;
	if (!wlr_region_confine(&current_constraint->wlr.region, x, y, x + *dx, y + *dy, &confined_x, &confined_y)) {
		wlr_log(WLR_ERROR, "Couldn't confine\n");
		return;
	}

	*dx = confined_x - x;
	*dy = confined_y - y;
}

bool Seat::is_pointer_locked(const wlr_pointer* pointer) const {
	return current_constraint != nullptr && pointer->base.type == WLR_INPUT_DEVICE_POINTER &&
		current_constraint->wlr.type == WLR_POINTER_CONSTRAINT_V1_LOCKED;
}
