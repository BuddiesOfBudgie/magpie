#include "seat.hpp"

#include "cursor.hpp"
#include "keyboard.hpp"
#include "server.hpp"
#include "types.hpp"

#include "wlr-wrap-start.hpp"
#include <wayland-util.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include "wlr-wrap-end.hpp"

void Seat::new_input_device(wlr_input_device* device) {
	switch (device->type) {
		case WLR_INPUT_DEVICE_KEYBOARD:
			keyboards.push_back(new Keyboard(*this, *wlr_keyboard_from_input_device(device)));
			break;
		case WLR_INPUT_DEVICE_POINTER:
		case WLR_INPUT_DEVICE_TABLET_TOOL:
		case WLR_INPUT_DEVICE_TOUCH:
			cursor->attach_input_device(device);
			break;
		default:
			break;
	}

	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!keyboards.empty()) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(seat, caps);
}

static void request_cursor_notify(wl_listener* listener, void* data) {
	const Seat& seat = magpie_container_of(listener, seat, request_cursor);
	auto* event = static_cast<wlr_seat_pointer_request_set_cursor_event*>(data);

	wlr_seat_client* focused_client = seat.seat->pointer_state.focused_client;

	if (focused_client == event->seat_client) {
		/* Once we've vetted the client, we can tell the cursor to use the
		 * provided surface as the cursor image. It will set the hardware cursor
		 * on the output that it's currently on and continue to do so as the
		 * cursor moves between outputs. */
		wlr_cursor_set_surface(seat.cursor->cursor, event->surface, event->hotspot_x, event->hotspot_y);
	}
}

/* This event is raised by the seat when a client wants to set the selection,
 * usually when the user copies something. wlroots allows compositors to
 * ignore such requests if they so choose, but in magpie we always honor
 */
static void request_set_selection_notify(wl_listener* listener, void* data) {
	const Seat& seat = magpie_container_of(listener, seat, request_set_selection);
	auto* event = static_cast<wlr_seat_request_set_selection_event*>(data);

	wlr_seat_set_selection(seat.seat, event->source, event->serial);
}

Seat::Seat(Server& server) noexcept : listeners(*this), server(server) {
	cursor = new Cursor(*this);
	seat = wlr_seat_create(server.display, "seat0");

	/*
	 * Configures a seat, which is a single "seat" at which a user sits and
	 * operates the computer. This conceptually includes up to one keyboard,
	 * pointer, touch, and drawing tablet device. We also rig up a listener to
	 * let us know when new input devices are available on the backend.
	 */
	listeners.request_cursor.notify = request_cursor_notify;
	wl_signal_add(&seat->events.request_set_cursor, &listeners.request_cursor);
	listeners.request_set_selection.notify = request_set_selection_notify;
	wl_signal_add(&seat->events.request_set_selection, &listeners.request_set_selection);
}