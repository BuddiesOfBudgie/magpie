#include "cursor.hpp"

#include "seat.hpp"
#include "server.hpp"
#include "view.hpp"

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>
#include "wlr-wrap-end.hpp"

void Cursor::process_resize(uint32_t time) {
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
	magpie_view_t* view = seat.server.grabbed_view;
	double border_x = wlr_cursor->x - seat.server.grab_x;
	double border_y = wlr_cursor->y - seat.server.grab_y;
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

	struct wlr_box geo_box;
	if (view->type == MAGPIE_VIEW_TYPE_XDG) {
		wlr_xdg_surface_get_geometry(view->xdg_view->xdg_toplevel->base, &geo_box);
	} else {
		geo_box.x = view->xwayland_view->xwayland_surface->x;
		geo_box.y = view->xwayland_view->xwayland_surface->y;
		geo_box.width = view->xwayland_view->xwayland_surface->width;
		geo_box.height = view->xwayland_view->xwayland_surface->height;
	}
	view->current.x = new_left - geo_box.x;
	view->current.y = new_top - geo_box.y;
	wlr_scene_node_set_position(&view->scene_tree->node, view->current.x, view->current.y);

	int new_width = new_right - new_left;
	int new_height = new_bottom - new_top;
	if (view->type == MAGPIE_VIEW_TYPE_XDG) {
		wlr_xdg_toplevel_set_size(view->xdg_view->xdg_toplevel, new_width, new_height);
	} else {
		wlr_xwayland_surface_configure(
			view->xwayland_view->xwayland_surface, view->current.x, view->current.y, new_width, new_height);
	}
}

void Cursor::process_move(uint32_t time) {
	(void) time;

	/* Move the grabbed view to the new position. */
	magpie_view_t* view = seat.server.grabbed_view;
	view->current.x = wlr_cursor->x - seat.server.grab_x;
	view->current.y = fmax(wlr_cursor->y - seat.server.grab_y, 0);
	wlr_xcursor_manager_set_cursor_image(cursor_mgr, "fleur", wlr_cursor);
	wlr_scene_node_set_position(&view->scene_tree->node, view->current.x, view->current.y);
}

void Cursor::process_motion(uint32_t time) {
	wlr_idle_notifier_v1_notify_activity(seat.server.idle_notifier, seat.wlr_seat);

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
	struct wlr_surface* surface = NULL;
	magpie_surface_t* magpie_surface = seat.server.surface_at(wlr_cursor->x, wlr_cursor->y, &surface, &sx, &sy);
	if (!magpie_surface) {
		/* If there's no view under the cursor, set the cursor image to a
		 * default. This is what makes the cursor image appear when you move it
		 * around the screen, not over any views. */
		wlr_xcursor_manager_set_cursor_image(cursor_mgr, "left_ptr", wlr_cursor);
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
		wlr_seat_pointer_notify_enter(seat.wlr_seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(seat.wlr_seat, time, sx, sy);
	} else {
		/* Clear pointer focus so future button events and such are not sent to
		 * the last client to have the cursor over it. */
		wlr_seat_pointer_clear_focus(seat.wlr_seat);
	}
}

void cursor_axis_notify(wl_listener* listener, void* data) {
	/* This event is forwarded by the cursor when a pointer emits an axis event,
	 * for example when you move the scroll wheel. */
	cursor_listener_container* container = wl_container_of(listener, container, axis);
	Cursor& cursor = *container->parent;

	struct wlr_pointer_axis_event* event = static_cast<struct wlr_pointer_axis_event*>(data);
	/* Notify the client with pointer focus of the axis event. */
	wlr_seat_pointer_notify_axis(
		cursor.seat.wlr_seat, event->time_msec, event->orientation, event->delta, event->delta_discrete, event->source);
}

void cursor_frame_notify(wl_listener* listener, void* data) {
	(void) data;

	/* This event is forwarded by the cursor when a pointer emits an frame
	 * event. Frame events are sent after regular pointer events to group
	 * multiple events together. For instance, two axis events may happen at the
	 * same time, in which case a frame event won't be sent in between. */
	cursor_listener_container* container = wl_container_of(listener, container, frame);
	Cursor& cursor = *container->parent;

	/* Notify the client with pointer focus of the frame event. */
	wlr_seat_pointer_notify_frame(cursor.seat.wlr_seat);
}

void cursor_motion_absolute_notify(wl_listener* listener, void* data) {
	/* This event is forwarded by the cursor when a pointer emits an _absolute_
	 * motion event, from 0..1 on each axis. This happens, for example, when
	 * wlroots is running under a Wayland window rather than KMS+DRM, and you
	 * move the mouse over the window. You could enter the window from any edge,
	 * so we have to warp the mouse there. There is also some hardware which
	 * emits these events. */
	cursor_listener_container* container = wl_container_of(listener, container, motion_absolute);
	Cursor& cursor = *container->parent;

	struct wlr_pointer_motion_absolute_event* event = static_cast<struct wlr_pointer_motion_absolute_event*>(data);
	wlr_cursor_warp_absolute(cursor.wlr_cursor, &event->pointer->base, event->x, event->y);
	cursor.process_motion(event->time_msec);
}

void Cursor::reset_mode() {
	/* Reset the cursor mode to passthrough. */
	if (mode != MAGPIE_CURSOR_PASSTHROUGH) {
		wlr_xcursor_manager_set_cursor_image(cursor_mgr, "left_ptr", wlr_cursor);
	}
	mode = MAGPIE_CURSOR_PASSTHROUGH;
	seat.server.grabbed_view = NULL;
}

void cursor_button_notify(wl_listener* listener, void* data) {
	/* This event is forwarded by the cursor when a pointer emits a button event. */
	cursor_listener_container* container = wl_container_of(listener, container, button);
	Cursor& cursor = *container->parent;

	Server& server = cursor.seat.server;
	struct wlr_pointer_button_event* event = static_cast<struct wlr_pointer_button_event*>(data);

	/* Notify the client with pointer focus that a button press has occurred */
	wlr_seat_pointer_notify_button(server.seat->wlr_seat, event->time_msec, event->button, event->state);
	double sx, sy;
	struct wlr_surface* surface = NULL;
	magpie_surface_t* magpie_surface = server.surface_at(cursor.wlr_cursor->x, cursor.wlr_cursor->y, &surface, &sx, &sy);
	if (event->state == WLR_BUTTON_RELEASED) {
		/* If you released any buttons, we exit interactive move/resize mode. */
		if (cursor.mode != MAGPIE_CURSOR_PASSTHROUGH) {
			cursor.reset_mode();
		}
	} else if (magpie_surface != NULL && magpie_surface->type == MAGPIE_SURFACE_TYPE_VIEW) {
		/* Focus that client if the button was _pressed_ */
		server.focus_view(magpie_surface->view, surface);
	}
}

void cursor_motion_notify(wl_listener* listener, void* data) {
	/* This event is forwarded by the cursor when a pointer emits a _relative_
	 * pointer motion event (i.e. a delta) */
	cursor_listener_container* container = wl_container_of(listener, container, motion);
	Cursor& cursor = *container->parent;

	struct wlr_pointer_motion_event* event = static_cast<struct wlr_pointer_motion_event*>(data);

	/* The cursor doesn't move unless we tell it to. The cursor automatically
	 * handles constraining the motion to the output layout, as well as any
	 * special configuration applied for the specific input device which
	 * generated the event. You can pass NULL for the device if you want to move
	 * the cursor around without any input. */
	wlr_cursor_move(cursor.wlr_cursor, &event->pointer->base, event->delta_x, event->delta_y);
	cursor.process_motion(event->time_msec);
}

void Cursor::attach_input_device(struct wlr_input_device* device) {
	wlr_cursor_attach_input_device(wlr_cursor, device);
}

Cursor::Cursor(Seat& seat) : seat(seat) {
	listeners.parent = this;

	/*
	 * Creates a cursor, which is a wlroots utility for tracking the cursor
	 * image shown on screen.
	 */
	wlr_cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(wlr_cursor, seat.server.output_layout);

	/* Creates an xcursor manager, another wlroots utility which loads up
	 * Xcursor themes to source cursor images from and makes sure that cursor
	 * images are available at all scale factors on the screen (necessary for
	 * HiDPI support). We add a cursor theme at scale factor 1 to begin with. */
	cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
	wlr_xcursor_manager_load(cursor_mgr, 1);

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
	wl_signal_add(&wlr_cursor->events.motion, &listeners.motion);
	listeners.motion_absolute.notify = cursor_motion_absolute_notify;
	wl_signal_add(&wlr_cursor->events.motion_absolute, &listeners.motion_absolute);
	listeners.button.notify = cursor_button_notify;
	wl_signal_add(&wlr_cursor->events.button, &listeners.button);
	listeners.axis.notify = cursor_axis_notify;
	wl_signal_add(&wlr_cursor->events.axis, &listeners.axis);
	listeners.frame.notify = cursor_frame_notify;
	wl_signal_add(&wlr_cursor->events.frame, &listeners.frame);
}
