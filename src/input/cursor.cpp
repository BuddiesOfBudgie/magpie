#include "cursor.hpp"

#include "input/constraint.hpp"
#include "seat.hpp"
#include "server.hpp"
#include "surface/surface.hpp"
#include "surface/view.hpp"

#include <cstring>
#include <iostream>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>
#include "wlr-wrap-end.hpp"

void Cursor::process_resize(const uint32_t time) {
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
	View& view = *seat.server.grabbed_view;
	double border_x = cursor->x - seat.server.grab_x;
	double border_y = cursor->y - seat.server.grab_y;
	int new_left = seat.server.grab_geobox.x;
	int new_right = seat.server.grab_geobox.x + seat.server.grab_geobox.width;
	int new_top = seat.server.grab_geobox.y;
	int new_bottom = seat.server.grab_geobox.y + seat.server.grab_geobox.height;

	if (seat.server.resize_edges & WLR_EDGE_TOP) {
		new_top = border_y;
		if (new_top >= new_bottom) {
			new_top = new_bottom - 1;
		}
	} else if (seat.server.resize_edges & WLR_EDGE_BOTTOM) {
		new_bottom = border_y;
		if (new_bottom <= new_top) {
			new_bottom = new_top + 1;
		}
	}
	if (seat.server.resize_edges & WLR_EDGE_LEFT) {
		new_left = border_x;
		if (new_left >= new_right) {
			new_left = new_right - 1;
		}
	} else if (seat.server.resize_edges & WLR_EDGE_RIGHT) {
		new_right = border_x;
		if (new_right <= new_left) {
			new_right = new_left + 1;
		}
	}

	wlr_box geo_box = view.get_geometry();
	view.set_position(new_left - geo_box.x, new_top - geo_box.y);

	int new_width = new_right - new_left;
	int new_height = new_bottom - new_top;
	view.set_size(new_width, new_height);
}

void Cursor::process_move(const uint32_t time) {
	(void) time;

	/* Move the grabbed view to the new position. */
	View* view = seat.server.grabbed_view;
	view->current.x = cursor->x - seat.server.grab_x;
	view->current.y = fmax(cursor->y - seat.server.grab_y, 0);

	set_image("fleur");
	wlr_scene_node_set_position(view->scene_node, view->current.x, view->current.y);
}

/* This event is forwarded by the cursor when a pointer emits an axis event,
 * for example when you move the scroll wheel. */
static void cursor_axis_notify(wl_listener* listener, void* data) {
	Cursor& cursor = magpie_container_of(listener, cursor, axis);
	auto* event = static_cast<wlr_pointer_axis_event*>(data);

	/* Notify the client with pointer focus of the axis event. */
	wlr_seat_pointer_notify_axis(
		cursor.seat.seat, event->time_msec, event->orientation, event->delta, event->delta_discrete, event->source);
}

/* This event is forwarded by the cursor when a pointer emits an frame
 * event. Frame events are sent after regular pointer events to group
 * multiple events together. For instance, two axis events may happen at the
 * same time, in which case a frame event won't be sent in between. */
static void cursor_frame_notify(wl_listener* listener, void* data) {
	Cursor& cursor = magpie_container_of(listener, cursor, frame);
	(void) data;

	/* Notify the client with pointer focus of the frame event. */
	wlr_seat_pointer_notify_frame(cursor.seat.seat);
}

/* This event is forwarded by the cursor when a pointer emits an _absolute_
 * motion event, from 0..1 on each axis. This happens, for example, when
 * wlroots is running under a Wayland window rather than KMS+DRM, and you
 * move the mouse over the window. You could enter the window from any edge,
 * so we have to warp the mouse there. There is also some hardware which
 * emits these events. */
static void cursor_motion_absolute_notify(wl_listener* listener, void* data) {
	Cursor& cursor = magpie_container_of(listener, cursor, motion_absolute);
	auto* event = static_cast<wlr_pointer_motion_absolute_event*>(data);

	double lx, ly;
	wlr_cursor_absolute_to_layout_coords(cursor.cursor, &event->pointer->base, event->x, event->y, &lx, &ly);

	double dx = lx - cursor.cursor->x;
	double dy = ly - cursor.cursor->y;
	wlr_relative_pointer_manager_v1_send_relative_motion(
		cursor.relative_pointer_mgr, cursor.seat.seat, (uint64_t) event->time_msec * 1000, dx, dy, dx, dy);

	if (cursor.seat.is_pointer_locked(event->pointer)) {
		return;
	}

	cursor.seat.apply_constraint(event->pointer, &dx, &dy);

	wlr_cursor_move(cursor.cursor, &event->pointer->base, dx, dy);
	cursor.process_motion(event->time_msec);
}

/* This event is forwarded by the cursor when a pointer emits a button event. */
static void cursor_button_notify(wl_listener* listener, void* data) {
	Cursor& cursor = magpie_container_of(listener, cursor, button);
	auto* event = static_cast<wlr_pointer_button_event*>(data);

	Server& server = cursor.seat.server;

	/* Notify the client with pointer focus that a button press has occurred */
	wlr_seat_pointer_notify_button(server.seat->seat, event->time_msec, event->button, event->state);
	double sx, sy;

	wlr_surface* surface = nullptr;
	Surface* magpie_surface = server.surface_at(cursor.cursor->x, cursor.cursor->y, &surface, &sx, &sy);

	if (event->state == WLR_BUTTON_RELEASED) {
		/* If you released any buttons, we exit interactive move/resize mode. */
		if (cursor.mode != MAGPIE_CURSOR_PASSTHROUGH) {
			cursor.reset_mode();
		}
	} else if (magpie_surface != nullptr && magpie_surface->is_view()) {
		/* Focus that client if the button was _pressed_ */
		server.focus_view(*static_cast<View*>(magpie_surface), surface);
	}
}

/* This event is forwarded by the cursor when a pointer emits a _relative_
 * pointer motion event (i.e. a delta) */
static void cursor_motion_notify(wl_listener* listener, void* data) {
	Cursor& cursor = magpie_container_of(listener, cursor, motion);
	auto* event = static_cast<wlr_pointer_motion_event*>(data);

	wlr_relative_pointer_manager_v1_send_relative_motion(cursor.relative_pointer_mgr, cursor.seat.seat,
		(uint64_t) event->time_msec * 1000, event->delta_x, event->delta_y, event->unaccel_dx, event->unaccel_dy);

	if (cursor.seat.is_pointer_locked(event->pointer)) {
		return;
	}

	double dx = event->delta_x;
	double dy = event->delta_y;
	cursor.seat.apply_constraint(event->pointer, &dx, &dy);

	wlr_cursor_move(cursor.cursor, &event->pointer->base, dx, dy);
	cursor.process_motion(event->time_msec);
}

static void gesture_pinch_begin_notify(wl_listener* listener, void* data) {
	Cursor& cursor = magpie_container_of(listener, cursor, gesture_pinch_begin);
	auto* event = static_cast<wlr_pointer_pinch_begin_event*>(data);

	wlr_pointer_gestures_v1_send_pinch_begin(cursor.pointer_gestures, cursor.seat.seat, event->time_msec, event->fingers);
}

static void gesture_pinch_update_notify(wl_listener* listener, void* data) {
	Cursor& cursor = magpie_container_of(listener, cursor, gesture_pinch_update);
	auto* event = static_cast<wlr_pointer_pinch_update_event*>(data);

	wlr_pointer_gestures_v1_send_pinch_update(
		cursor.pointer_gestures, cursor.seat.seat, event->time_msec, event->dx, event->dy, event->scale, event->rotation);
}

static void gesture_pinch_end_notify(wl_listener* listener, void* data) {
	Cursor& cursor = magpie_container_of(listener, cursor, gesture_pinch_end);
	auto* event = static_cast<wlr_pointer_pinch_end_event*>(data);

	wlr_pointer_gestures_v1_send_pinch_end(cursor.pointer_gestures, cursor.seat.seat, event->time_msec, event->cancelled);
}

static void gesture_swipe_begin_notify(wl_listener* listener, void* data) {
	Cursor& cursor = magpie_container_of(listener, cursor, gesture_swipe_begin);
	auto* event = static_cast<wlr_pointer_swipe_begin_event*>(data);

	wlr_pointer_gestures_v1_send_swipe_begin(cursor.pointer_gestures, cursor.seat.seat, event->time_msec, event->fingers);
}

static void gesture_swipe_update_notify(wl_listener* listener, void* data) {
	Cursor& cursor = magpie_container_of(listener, cursor, gesture_swipe_update);
	auto* event = static_cast<wlr_pointer_swipe_update_event*>(data);

	wlr_pointer_gestures_v1_send_swipe_update(
		cursor.pointer_gestures, cursor.seat.seat, event->time_msec, event->dx, event->dy);
}

static void gesture_swipe_end_notify(wl_listener* listener, void* data) {
	Cursor& cursor = magpie_container_of(listener, cursor, gesture_swipe_end);
	auto* event = static_cast<wlr_pointer_swipe_end_event*>(data);

	wlr_pointer_gestures_v1_send_swipe_end(cursor.pointer_gestures, cursor.seat.seat, event->time_msec, event->cancelled);
}

static void gesture_hold_begin_notify(wl_listener* listener, void* data) {
	Cursor& cursor = magpie_container_of(listener, cursor, gesture_hold_begin);
	auto* event = static_cast<wlr_pointer_hold_begin_event*>(data);

	wlr_pointer_gestures_v1_send_hold_begin(cursor.pointer_gestures, cursor.seat.seat, event->time_msec, event->fingers);
}

static void gesture_hold_end_notify(wl_listener* listener, void* data) {
	Cursor& cursor = magpie_container_of(listener, cursor, gesture_hold_end);
	auto* event = static_cast<wlr_pointer_hold_end_event*>(data);

	wlr_pointer_gestures_v1_send_hold_end(cursor.pointer_gestures, cursor.seat.seat, event->time_msec, event->cancelled);
}

Cursor::Cursor(Seat& seat) noexcept : listeners(*this), seat(seat) {
	/*
	 * Creates a cursor, which is a wlroots utility for tracking the cursor
	 * image shown on screen.
	 */
	cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(cursor, seat.server.output_layout);

	/* Creates an xcursor manager, another wlroots utility which loads up
	 * Xcursor themes to source cursor images from and makes sure that cursor
	 * images are available at all scale factors on the screen (necessary for
	 * HiDPI support). We add a cursor theme at scale factor 1 to begin with. */
	cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
	wlr_xcursor_manager_load(cursor_mgr, 1);

	relative_pointer_mgr = wlr_relative_pointer_manager_v1_create(seat.server.display);
	pointer_gestures = wlr_pointer_gestures_v1_create(seat.server.display);

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
	wl_signal_add(&cursor->events.motion, &listeners.motion);
	listeners.motion_absolute.notify = cursor_motion_absolute_notify;
	wl_signal_add(&cursor->events.motion_absolute, &listeners.motion_absolute);
	listeners.button.notify = cursor_button_notify;
	wl_signal_add(&cursor->events.button, &listeners.button);
	listeners.axis.notify = cursor_axis_notify;
	wl_signal_add(&cursor->events.axis, &listeners.axis);
	listeners.frame.notify = cursor_frame_notify;
	wl_signal_add(&cursor->events.frame, &listeners.frame);

	listeners.gesture_pinch_begin.notify = gesture_pinch_begin_notify;
	wl_signal_add(&cursor->events.pinch_begin, &listeners.gesture_pinch_begin);
	listeners.gesture_pinch_update.notify = gesture_pinch_update_notify;
	wl_signal_add(&cursor->events.pinch_update, &listeners.gesture_pinch_update);
	listeners.gesture_pinch_end.notify = gesture_pinch_end_notify;
	wl_signal_add(&cursor->events.pinch_end, &listeners.gesture_pinch_end);
	listeners.gesture_swipe_begin.notify = gesture_swipe_begin_notify;
	wl_signal_add(&cursor->events.swipe_begin, &listeners.gesture_swipe_begin);
	listeners.gesture_swipe_update.notify = gesture_swipe_update_notify;
	wl_signal_add(&cursor->events.swipe_update, &listeners.gesture_swipe_update);
	listeners.gesture_swipe_end.notify = gesture_swipe_end_notify;
	wl_signal_add(&cursor->events.swipe_end, &listeners.gesture_swipe_end);
	listeners.gesture_hold_begin.notify = gesture_hold_begin_notify;
	wl_signal_add(&cursor->events.hold_begin, &listeners.gesture_swipe_update);
	listeners.gesture_hold_end.notify = gesture_hold_end_notify;
	wl_signal_add(&cursor->events.hold_end, &listeners.gesture_swipe_end);
}

void Cursor::attach_input_device(wlr_input_device* device) {
	wlr_cursor_attach_input_device(cursor, device);
}

void Cursor::process_motion(const uint32_t time) {
	wlr_idle_notifier_v1_notify_activity(seat.server.idle_notifier, seat.seat);

	/* If the mode is non-passthrough, delegate to those functions. */
	if (mode == MAGPIE_CURSOR_MOVE) {
		process_move(time);
		return;
	} else if (mode == MAGPIE_CURSOR_RESIZE) {
		process_resize(time);
		return;
	}

	/* Otherwise, find the view under the pointer and send the event along. */
	double sx, sy;
	wlr_surface* surface = NULL;
	Surface* magpie_surface = seat.server.surface_at(cursor->x, cursor->y, &surface, &sx, &sy);
	if (!magpie_surface) {
		/* If there's no view under the cursor, set the cursor image to a
		 * default. This is what makes the cursor image appear when you move it
		 * around the screen, not over any views. */
		set_image("left_ptr");
	}
	if (surface) {
		/*
		 * Send pointer enter and motion events.
		 *
		 * The enter event gives the surface "pointer focus", which is distinct
		 * from keyboard focus. You get pointer focus by moving the pointer over
		 * a window.
		 *
		 * Note that wlroots will avoid sending duplicate enter/motion events if
		 * the surface has already has pointer focus or if the client is already
		 * aware of the coordinates passed.
		 */
		wlr_seat_pointer_notify_enter(seat.seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(seat.seat, time, sx, sy);
	} else {
		/* Clear pointer focus so future button events and such are not sent to
		 * the last client to have the cursor over it. */
		wlr_seat_pointer_clear_focus(seat.seat);
	}
}

void Cursor::reset_mode() {
	if (mode != MAGPIE_CURSOR_PASSTHROUGH) {
		set_image("left_ptr");
	}
	mode = MAGPIE_CURSOR_PASSTHROUGH;
	seat.server.grabbed_view = NULL;
}

void Cursor::warp_to_constraint(PointerConstraint& constraint) {
	if (seat.server.focused_view->surface != constraint.wlr->surface) {
		return;
	}

	if (seat.server.focused_view == nullptr) {
		// only warp to constraints tied to views...
		return;
	}

	if (constraint.wlr->current.committed & WLR_POINTER_CONSTRAINT_V1_STATE_CURSOR_HINT) {
		double x = constraint.wlr->current.cursor_hint.x;
		double y = constraint.wlr->current.cursor_hint.y;

		wlr_cursor_warp(cursor, nullptr, seat.server.focused_view->current.x + x, seat.server.focused_view->current.y + y);
		wlr_seat_pointer_warp(seat.seat, x, y);
	}
}

void Cursor::set_image(const std::string name) {
	if (current_image != name) {
		wlr_xcursor_manager_set_cursor_image(cursor_mgr, name.c_str(), cursor);
		current_image = name;
	}
}
