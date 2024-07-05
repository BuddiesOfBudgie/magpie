#include "cursor.hpp"

#include "input/constraint.hpp"
#include "output.hpp"
#include "seat.hpp"
#include "server.hpp"
#include "surface/surface.hpp"
#include "surface/view.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include "wlr-wrap-end.hpp"

void Cursor::process_resize(const uint32_t time) const {
	(void) time;

	/*
	 * Resizing the grabbed view can be a little bit complicated, because we
	 * could be resizing from any corner or edge. This not only resizes the view
	 * on one or two axes, but can also move the view if you resize from the top
	 * or left edges (or top-left corner).
	 *
	 * Note that I took some shortcuts here. In a more fleshed-out compositor,
	 * you'd wait for the client to prepare a buffer at the new size, then
	 * commit any movement that was prepared.
	 */
	std::shared_ptr<View> view = seat.server.grabbed_view.lock();

	if (view == nullptr) {
		wlr_log(WLR_ERROR, "Attempted to process_resize without a grabbed view");
		return;
	}

	const wlr_box min_size = view->get_min_size();
	const wlr_box max_size = view->get_max_size();
	const double border_x = wlr.x - seat.server.grab_x;
	const double border_y = wlr.y - seat.server.grab_y;
	int32_t new_left = seat.server.grab_geobox.x;
	int32_t new_right = seat.server.grab_geobox.x + seat.server.grab_geobox.width;
	int32_t new_top = seat.server.grab_geobox.y;
	int32_t new_bottom = seat.server.grab_geobox.y + seat.server.grab_geobox.height;

	if ((seat.server.resize_edges & WLR_EDGE_TOP) != 0) {
		new_top = static_cast<int32_t>(std::round(border_y));
		if (new_top >= new_bottom) {
			new_top = new_bottom - 1;
		}
	} else if ((seat.server.resize_edges & WLR_EDGE_BOTTOM) != 0) {
		new_bottom = static_cast<int32_t>(std::round(border_y));
		if (new_bottom <= new_top) {
			new_bottom = new_top + 1;
		}
	}
	if ((seat.server.resize_edges & WLR_EDGE_LEFT) != 0) {
		new_left = static_cast<int32_t>(std::round(border_x));
		if (new_left >= new_right) {
			new_left = new_right - 1;
		}
	} else if ((seat.server.resize_edges & WLR_EDGE_RIGHT) != 0) {
		new_right = static_cast<int32_t>(std::round(border_x));
		if (new_right <= new_left) {
			new_right = new_left + 1;
		}
	}

	const wlr_box geo_box = view->get_geometry();
	const int32_t new_width = std::clamp(new_right - new_left, min_size.width, max_size.width);
	const int32_t new_height = std::clamp(new_bottom - new_top, min_size.height, max_size.height);
	const int32_t new_x = new_width == view->current.width ? view->current.x : new_left - geo_box.x;
	const int32_t new_y = new_height == view->current.height ? view->current.y : new_top - geo_box.y;
	view->set_geometry(new_x, new_y, new_width, new_height);

	view->update_outputs();
}

void Cursor::process_move(const uint32_t time) {
	(void) time;

	set_image("fleur");

	/* Move the grabbed view to the new position. */
	std::shared_ptr<View> view = seat.server.grabbed_view.lock();

	if (view == nullptr) {
		wlr_log(WLR_ERROR, "Attempted to process_move without a grabbed view");
		return;
	}

	const auto new_x = static_cast<int32_t>(std::round(wlr.x - seat.server.grab_x));
	const auto new_y = static_cast<int32_t>(std::round(std::fmax(wlr.y - seat.server.grab_y, 0)));
	view->set_position(new_x, new_y);

	view->update_outputs();
}

/* This event is forwarded by the cursor when a pointer emits an axis event,
 * for example when you move the scroll wheel. */
static void cursor_axis_notify(wl_listener* listener, void* data) {
	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_cursor.events.cursor_axis");
		return;
	}

	Cursor& cursor = magpie_container_of(listener, cursor, axis);
	const auto* event = static_cast<wlr_pointer_axis_event*>(data);

	/* Notify the client with pointer focus of the axis event. */
	wlr_seat_pointer_notify_axis(
		cursor.seat.wlr, event->time_msec, event->orientation, event->delta, event->delta_discrete, event->source);
}

/* This event is forwarded by the cursor when a pointer emits an frame
 * event. Frame events are sent after regular pointer events to group
 * multiple events together. For instance, two axis events may happen at the
 * same time, in which case a frame event won't be sent in between. */
static void cursor_frame_notify(wl_listener* listener, [[maybe_unused]] void* data) {
	Cursor& cursor = magpie_container_of(listener, cursor, frame);

	/* Notify the client with pointer focus of the frame event. */
	wlr_seat_pointer_notify_frame(cursor.seat.wlr);
}

/* This event is forwarded by the cursor when a pointer emits an _absolute_
 * motion event, from 0..1 on each axis. This happens, for example, when
 * wlroots is running under a Wayland window rather than KMS+DRM, and you
 * move the mouse over the window. You could enter the window from any edge,
 * so we have to warp the mouse there. There is also some hardware which
 * emits these events. */
static void cursor_motion_absolute_notify(wl_listener* listener, void* data) {
	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_cursor.events.motion_absolute");
		return;
	}

	Cursor& cursor = magpie_container_of(listener, cursor, motion_absolute);
	const auto* event = static_cast<wlr_pointer_motion_absolute_event*>(data);

	double lx;
	double ly;
	wlr_cursor_absolute_to_layout_coords(&cursor.wlr, &event->pointer->base, event->x, event->y, &lx, &ly);

	double dx = lx - cursor.wlr.x;
	double dy = ly - cursor.wlr.y;
	wlr_relative_pointer_manager_v1_send_relative_motion(
		cursor.relative_pointer_mgr, cursor.seat.wlr, static_cast<uint64_t>(event->time_msec) * 1000, dx, dy, dx, dy);

	if (cursor.seat.is_pointer_locked(event->pointer)) {
		return;
	}

	cursor.seat.apply_constraint(event->pointer, &dx, &dy);

	wlr_cursor_move(&cursor.wlr, &event->pointer->base, dx, dy);
	cursor.process_motion(event->time_msec);
}

/* This event is forwarded by the cursor when a pointer emits a button event. */
static void cursor_button_notify(wl_listener* listener, void* data) {
	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_cursor.events.button");
		return;
	}

	Cursor& cursor = magpie_container_of(listener, cursor, button);
	const auto* event = static_cast<wlr_pointer_button_event*>(data);

	Server& server = cursor.seat.server;

	/* Notify the client with pointer focus that a button press has occurred */
	wlr_seat_pointer_notify_button(server.seat->wlr, event->time_msec, event->button, event->state);
	double sx;
	double sy;

	wlr_surface* surface = nullptr;
	auto magpie_surface = server.surface_at(cursor.wlr.x, cursor.wlr.y, &surface, &sx, &sy).lock();

	if (event->state == WLR_BUTTON_RELEASED) {
		/* If you released any buttons, we exit interactive move/resize mode. */
		if (cursor.mode != MAGPIE_CURSOR_PASSTHROUGH) {
			cursor.reset_mode();
		}
	} else if (magpie_surface != nullptr && magpie_surface->is_view()) {
		/* Focus that client if the button was _pressed_ */
		server.focus_view(std::dynamic_pointer_cast<View>(magpie_surface));
	} else {
		server.focus_view(nullptr);
	}
}

/* This event is forwarded by the cursor when a pointer emits a _relative_
 * pointer motion event (i.e. a delta) */
static void cursor_motion_notify(wl_listener* listener, void* data) {
	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_cursor.events.motion");
		return;
	}

	Cursor& cursor = magpie_container_of(listener, cursor, motion);
	const auto* event = static_cast<wlr_pointer_motion_event*>(data);

	wlr_relative_pointer_manager_v1_send_relative_motion(cursor.relative_pointer_mgr, cursor.seat.wlr,
		static_cast<uint64_t>(event->time_msec) * 1000, event->delta_x, event->delta_y, event->unaccel_dx, event->unaccel_dy);

	if (cursor.seat.is_pointer_locked(event->pointer)) {
		return;
	}

	double dx = event->delta_x;
	double dy = event->delta_y;
	cursor.seat.apply_constraint(event->pointer, &dx, &dy);

	wlr_cursor_move(&cursor.wlr, &event->pointer->base, dx, dy);
	cursor.process_motion(event->time_msec);
}

static void gesture_pinch_begin_notify(wl_listener* listener, void* data) {
	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_cursor.events.pinch_begin");
		return;
	}

	Cursor& cursor = magpie_container_of(listener, cursor, gesture_pinch_begin);
	const auto* event = static_cast<wlr_pointer_pinch_begin_event*>(data);

	wlr_pointer_gestures_v1_send_pinch_begin(cursor.pointer_gestures, cursor.seat.wlr, event->time_msec, event->fingers);
}

static void gesture_pinch_update_notify(wl_listener* listener, void* data) {
	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_cursor.events.pinch_update");
		return;
	}

	Cursor& cursor = magpie_container_of(listener, cursor, gesture_pinch_update);
	const auto* event = static_cast<wlr_pointer_pinch_update_event*>(data);

	wlr_pointer_gestures_v1_send_pinch_update(
		cursor.pointer_gestures, cursor.seat.wlr, event->time_msec, event->dx, event->dy, event->scale, event->rotation);
}

static void gesture_pinch_end_notify(wl_listener* listener, void* data) {
	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_cursor.events.pinch_end");
		return;
	}

	Cursor& cursor = magpie_container_of(listener, cursor, gesture_pinch_end);
	const auto* event = static_cast<wlr_pointer_pinch_end_event*>(data);

	wlr_pointer_gestures_v1_send_pinch_end(cursor.pointer_gestures, cursor.seat.wlr, event->time_msec, event->cancelled);
}

static void gesture_swipe_begin_notify(wl_listener* listener, void* data) {
	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_cursor.events.swipe_begin");
		return;
	}

	Cursor& cursor = magpie_container_of(listener, cursor, gesture_swipe_begin);
	const auto* event = static_cast<wlr_pointer_swipe_begin_event*>(data);

	wlr_pointer_gestures_v1_send_swipe_begin(cursor.pointer_gestures, cursor.seat.wlr, event->time_msec, event->fingers);
}

static void gesture_swipe_update_notify(wl_listener* listener, void* data) {
	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_cursor.events.swipe_update");
		return;
	}

	Cursor& cursor = magpie_container_of(listener, cursor, gesture_swipe_update);
	const auto* event = static_cast<wlr_pointer_swipe_update_event*>(data);

	wlr_pointer_gestures_v1_send_swipe_update(cursor.pointer_gestures, cursor.seat.wlr, event->time_msec, event->dx, event->dy);
}

static void gesture_swipe_end_notify(wl_listener* listener, void* data) {
	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_cursor.events.swipe_end");
		return;
	}

	Cursor& cursor = magpie_container_of(listener, cursor, gesture_swipe_end);
	const auto* event = static_cast<wlr_pointer_swipe_end_event*>(data);

	wlr_pointer_gestures_v1_send_swipe_end(cursor.pointer_gestures, cursor.seat.wlr, event->time_msec, event->cancelled);
}

static void gesture_hold_begin_notify(wl_listener* listener, void* data) {
	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_cursor.events.hold_begin");
		return;
	}

	Cursor& cursor = magpie_container_of(listener, cursor, gesture_hold_begin);
	const auto* event = static_cast<wlr_pointer_hold_begin_event*>(data);

	wlr_pointer_gestures_v1_send_hold_begin(cursor.pointer_gestures, cursor.seat.wlr, event->time_msec, event->fingers);
}

static void gesture_hold_end_notify(wl_listener* listener, void* data) {
	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_cursor.events.hold_end");
		return;
	}

	Cursor& cursor = magpie_container_of(listener, cursor, gesture_hold_end);
	const auto* event = static_cast<wlr_pointer_hold_end_event*>(data);

	wlr_pointer_gestures_v1_send_hold_end(cursor.pointer_gestures, cursor.seat.wlr, event->time_msec, event->cancelled);
}

static void request_set_shape_notify(wl_listener* listener, void* data) {
	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_cursor.events.set_shape");
		return;
	}

	Cursor& cursor = magpie_container_of(listener, cursor, request_set_shape);
	const auto* event = static_cast<wlr_cursor_shape_manager_v1_request_set_shape_event*>(data);

	cursor.set_image(wlr_cursor_shape_v1_name(event->shape));
}

Cursor::Cursor(Seat& seat) noexcept : listeners(*this), seat(seat), wlr(*wlr_cursor_create()) {
	/*
	 * Creates a cursor, which is a wlroots utility for tracking the cursor
	 * image shown on screen.
	 */
	wlr_cursor_attach_output_layout(&wlr, seat.server.output_layout);

	/* Creates an xcursor manager, another wlroots utility which loads up
	 * Xcursor themes to source cursor images from and makes sure that cursor
	 * images are available at all scale factors on the screen (necessary for
	 * HiDPI support). We add a cursor theme at scale factor 1 to begin with. */
	cursor_mgr = wlr_xcursor_manager_create(nullptr, 24);
	wlr_xcursor_manager_load(cursor_mgr, 1);

	relative_pointer_mgr = wlr_relative_pointer_manager_v1_create(seat.server.display);
	pointer_gestures = wlr_pointer_gestures_v1_create(seat.server.display);
	shape_mgr = wlr_cursor_shape_manager_v1_create(seat.server.display, 1);

	/*
	 * wlr_cursor *only* displays an image on screen. It does not move around
	 * when the pointer moves. However, we can attach input devices to it, and
	 * it will generate aggregate events for all of them. In these events, we
	 * can choose how we want to process them, forwarding them to clients and
	 * moving the cursor around. More detail on this process is described in my
	 * input handling blog post:
	 *
	 * https://drewdevault.com/2018/07/17/Input-handling-in-wlroots.html
	 *
	 * And more comments are sprinkled throughout the notify functions above.
	 */
	mode = MAGPIE_CURSOR_PASSTHROUGH;
	listeners.motion.notify = cursor_motion_notify;
	wl_signal_add(&wlr.events.motion, &listeners.motion);
	listeners.motion_absolute.notify = cursor_motion_absolute_notify;
	wl_signal_add(&wlr.events.motion_absolute, &listeners.motion_absolute);
	listeners.button.notify = cursor_button_notify;
	wl_signal_add(&wlr.events.button, &listeners.button);
	listeners.axis.notify = cursor_axis_notify;
	wl_signal_add(&wlr.events.axis, &listeners.axis);
	listeners.frame.notify = cursor_frame_notify;
	wl_signal_add(&wlr.events.frame, &listeners.frame);

	listeners.gesture_pinch_begin.notify = gesture_pinch_begin_notify;
	wl_signal_add(&wlr.events.pinch_begin, &listeners.gesture_pinch_begin);
	listeners.gesture_pinch_update.notify = gesture_pinch_update_notify;
	wl_signal_add(&wlr.events.pinch_update, &listeners.gesture_pinch_update);
	listeners.gesture_pinch_end.notify = gesture_pinch_end_notify;
	wl_signal_add(&wlr.events.pinch_end, &listeners.gesture_pinch_end);
	listeners.gesture_swipe_begin.notify = gesture_swipe_begin_notify;
	wl_signal_add(&wlr.events.swipe_begin, &listeners.gesture_swipe_begin);
	listeners.gesture_swipe_update.notify = gesture_swipe_update_notify;
	wl_signal_add(&wlr.events.swipe_update, &listeners.gesture_swipe_update);
	listeners.gesture_swipe_end.notify = gesture_swipe_end_notify;
	wl_signal_add(&wlr.events.swipe_end, &listeners.gesture_swipe_end);
	listeners.gesture_hold_begin.notify = gesture_hold_begin_notify;
	wl_signal_add(&wlr.events.hold_begin, &listeners.gesture_swipe_update);
	listeners.gesture_hold_end.notify = gesture_hold_end_notify;
	wl_signal_add(&wlr.events.hold_end, &listeners.gesture_swipe_end);

	listeners.request_set_shape.notify = request_set_shape_notify;
	wl_signal_add(&shape_mgr->events.request_set_shape, &listeners.request_set_shape);
}

void Cursor::attach_input_device(wlr_input_device* device) const {
	wlr_cursor_attach_input_device(&wlr, device);
}

void Cursor::process_motion(const uint32_t time) {
	wlr_idle_notifier_v1_notify_activity(seat.server.idle_notifier, seat.wlr);

	/* If the mode is non-passthrough, delegate to those functions. */
	if (mode == MAGPIE_CURSOR_MOVE) {
		process_move(time);
		return;
	}

	if (mode == MAGPIE_CURSOR_RESIZE) {
		process_resize(time);
		return;
	}

	/* Otherwise, find the view under the pointer and send the event along. */
	double sx;
	double sy;
	wlr_surface* surface = nullptr;
	auto magpie_surface = seat.server.surface_at(wlr.x, wlr.y, &surface, &sx, &sy).lock();
	if (magpie_surface == nullptr) {
		/* If there's no view under the cursor, set the cursor image to a
		 * default. This is what makes the cursor image appear when you move it
		 * around the screen, not over any views. */
		set_image("left_ptr");
	}

	if (surface != nullptr) {
		/*
		 * Send pointer enter and motion events.
		 *
		 * The enter event gives the surface "pointer focus", which is distinct
		 * from keyboard focus. You get pointer focus by moving the pointer over
		 * a window.
		 *
		 * Note that wlroots will avoid sending duplicate enter/motion events if
		 * the surface has already had pointer focus or if the client is already
		 * aware of the coordinates passed.
		 */
		current_image = "";
		wlr_seat_pointer_notify_enter(seat.wlr, surface, sx, sy);
		wlr_seat_pointer_notify_motion(seat.wlr, time, sx, sy);
	} else {
		/* Clear pointer focus so future button events and such are not sent to
		 * the last client to have the cursor over it. */
		wlr_seat_pointer_clear_focus(seat.wlr);
	}
}

void Cursor::reset_mode() {
	if (mode != MAGPIE_CURSOR_PASSTHROUGH) {
		set_image("left_ptr");
	}
	mode = MAGPIE_CURSOR_PASSTHROUGH;
	seat.server.grabbed_view.reset();
}

void Cursor::warp_to_constraint(const PointerConstraint& constraint) const {
	auto focused_view = seat.server.focused_view.lock();

	if (focused_view == nullptr) {
		// only warp to constraints tied to views...
		return;
	}

	if (focused_view->get_wlr_surface() != constraint.wlr.surface) {
		return;
	}

	if ((constraint.wlr.current.committed & WLR_POINTER_CONSTRAINT_V1_STATE_CURSOR_HINT) != 0) {
		const double x = constraint.wlr.current.cursor_hint.x;
		const double y = constraint.wlr.current.cursor_hint.y;

		wlr_cursor_warp(&wlr, nullptr, focused_view->current.x + x, focused_view->current.y + y);
		wlr_seat_pointer_warp(seat.wlr, x, y);
	}
}

void Cursor::set_image(const std::string& name) {
	if (current_image != name) {
		current_image = name;
		reload_image();
	}
}

void Cursor::reload_image() const {
	wlr_cursor_set_xcursor(&wlr, cursor_mgr, current_image.c_str());
}
