#include "input.hpp"
#include "server.hpp"
#include "surface.hpp"
#include "types.hpp"
#include "view.hpp"

#include <cstdlib>

#include "wlr-wrap-start.hpp"
#include <wlr/backend/libinput.h>
#include <wlr/backend/multi.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_idle.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>
#include "wlr-wrap-end.hpp"

static void process_cursor_resize(magpie_server_t* server, uint32_t time) {
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
	magpie_view_t* view = server->grabbed_view;
	double border_x = server->cursor->x - server->grab_x;
	double border_y = server->cursor->y - server->grab_y;
	int new_left = server->grab_geobox.x;
	int new_right = server->grab_geobox.x + server->grab_geobox.width;
	int new_top = server->grab_geobox.y;
	int new_bottom = server->grab_geobox.y + server->grab_geobox.height;

	if (server->resize_edges & WLR_EDGE_TOP) {
		new_top = border_y;
		if (new_top >= new_bottom) {
			new_top = new_bottom - 1;
		}
	} else if (server->resize_edges & WLR_EDGE_BOTTOM) {
		new_bottom = border_y;
		if (new_bottom <= new_top) {
			new_bottom = new_top + 1;
		}
	}
	if (server->resize_edges & WLR_EDGE_LEFT) {
		new_left = border_x;
		if (new_left >= new_right) {
			new_left = new_right - 1;
		}
	} else if (server->resize_edges & WLR_EDGE_RIGHT) {
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

static void process_cursor_move(magpie_server_t* server, uint32_t time) {
	(void) time;

	/* Move the grabbed view to the new position. */
	magpie_view_t* view = server->grabbed_view;
	view->current.x = server->cursor->x - server->grab_x;
	view->current.y = fmax(server->cursor->y - server->grab_y, 0);
	wlr_xcursor_manager_set_cursor_image(server->cursor_mgr, "fleur", server->cursor);
	wlr_scene_node_set_position(&view->scene_tree->node, view->current.x, view->current.y);
}

static void process_cursor_motion(magpie_server_t* server, uint32_t time) {
	wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);

	/* If the mode is non-passthrough, delegate to those functions. */
	if (server->cursor_mode == MAGPIE_CURSOR_MOVE) {
		process_cursor_move(server, time);
		return;
	} else if (server->cursor_mode == MAGPIE_CURSOR_RESIZE) {
		process_cursor_resize(server, time);
		return;
	}

	/* Otherwise, find the view under the pointer and send the event along. */
	double sx, sy;
	struct wlr_seat* seat = server->seat;
	struct wlr_surface* surface = NULL;
	magpie_surface_t* magpie_surface = surface_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	if (!magpie_surface) {
		/* If there's no view under the cursor, set the cursor image to a
		 * default. This is what makes the cursor image appear when you move it
		 * around the screen, not over any views. */
		wlr_xcursor_manager_set_cursor_image(server->cursor_mgr, "left_ptr", server->cursor);
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
		wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(seat, time, sx, sy);
	} else {
		/* Clear pointer focus so future button events and such are not sent to
		 * the last client to have the cursor over it. */
		wlr_seat_pointer_clear_focus(seat);
	}
}

void reset_cursor_mode(magpie_server_t* server) {
	/* Reset the cursor mode to passthrough. */
	if (server->cursor_mode != MAGPIE_CURSOR_PASSTHROUGH) {
		wlr_xcursor_manager_set_cursor_image(server->cursor_mgr, "left_ptr", server->cursor);
	}
	server->cursor_mode = MAGPIE_CURSOR_PASSTHROUGH;
	server->grabbed_view = NULL;
}

static bool handle_compositor_keybinding(magpie_keyboard_t* keyboard, uint32_t modifiers, xkb_keysym_t sym) {
	magpie_server_t* server = keyboard->server;

	if (modifiers == WLR_MODIFIER_ALT) {
		switch (sym) {
			case XKB_KEY_Escape:
				wl_display_terminate(server->wl_display);
				return true;
			case XKB_KEY_Tab:
				/* Cycle to the next view */
				if (wl_list_length(&server->views) < 2) {
					return true;
				}
				magpie_view_t* next_view = wl_container_of(server->views.prev, next_view, link);
				focus_view(next_view, next_view->xdg_view->xdg_toplevel->base->surface);
				return true;
		}
	} else if (sym >= XKB_KEY_XF86Switch_VT_1 && sym <= XKB_KEY_XF86Switch_VT_12) {
		if (wlr_backend_is_multi(keyboard->server->backend)) {
			struct wlr_session* session = wlr_backend_get_session(keyboard->server->backend);
			if (session) {
				unsigned vt = sym - XKB_KEY_XF86Switch_VT_1 + 1;
				wlr_session_change_vt(session, vt);
			}
		}
		return true;
	}

	return false;
}

static void keyboard_handle_destroy(struct wl_listener* listener, void* data) {
	(void) data;

	/* This event is raised by the keyboard base wlr_input_device to signal
	 * the destruction of the wlr_keyboard. It will no longer receive events
	 * and should be destroyed.
	 */
	magpie_keyboard_t* keyboard = wl_container_of(listener, keyboard, destroy);
	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	free(keyboard);
}

static void keyboard_handle_key(struct wl_listener* listener, void* data) {
	/* This event is raised when a key is pressed or released. */
	magpie_keyboard_t* keyboard = wl_container_of(listener, keyboard, key);
	magpie_server_t* server = keyboard->server;
	struct wlr_keyboard_key_event* event = static_cast<struct wlr_keyboard_key_event*>(data);
	struct wlr_seat* seat = server->seat;

	wlr_idle_notifier_v1_notify_activity(server->idle_notifier, seat);

	/* Translate libinput keycode -> xkbcommon */
	uint32_t keycode = event->keycode + 8;
	/* Get a list of keysyms based on the keymap for this keyboard */
	const xkb_keysym_t* syms;
	int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &syms);

	bool handled = false;
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
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
		wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
		wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
	}
}

static void keyboard_handle_modifiers(struct wl_listener* listener, void* data) {
	(void) data;

	/* This event is raised when a modifier key, such as shift or alt, is
	 * pressed. We simply communicate this to the client. */
	magpie_keyboard_t* keyboard = wl_container_of(listener, keyboard, modifiers);
	/*
	 * A seat can only have one keyboard, but this is a limitation of the
	 * Wayland protocol - not wlroots. We assign all connected keyboards to the
	 * same seat. You can swap out the underlying wlr_keyboard like this and
	 * wlr_seat handles this transparently.
	 */
	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
	/* Send modifiers to the client. */
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat, &keyboard->wlr_keyboard->modifiers);
}

static void new_pointer(magpie_server_t* server, struct wlr_input_device* device) {
	wlr_cursor_attach_input_device(server->cursor, device);
}

static void new_keyboard(magpie_server_t* server, struct wlr_input_device* device) {
	struct wlr_keyboard* wlr_keyboard = wlr_keyboard_from_input_device(device);

	magpie_keyboard_t* keyboard = (magpie_keyboard_t*) std::calloc(1, sizeof(magpie_keyboard_t));
	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;

	/* We need to prepare an XKB keymap and assign it to the keyboard. This
	 * assumes the defaults (e.g. layout = "us"). */
	struct xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap* keymap = xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);

	wlr_keyboard_set_keymap(wlr_keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

	/* Here we set up listeners for keyboard events. */
	keyboard->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keyboard_handle_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
	keyboard->destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&device->events.destroy, &keyboard->destroy);

	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);

	/* And add the keyboard to our list of keyboards */
	wl_list_insert(&server->keyboards, &keyboard->link);
}

void new_input_notify(struct wl_listener* listener, void* data) {
	magpie_server_t* server = wl_container_of(listener, server, new_input);
	struct wlr_input_device* device = static_cast<struct wlr_input_device*>(data);
	switch (device->type) {
		case WLR_INPUT_DEVICE_KEYBOARD:
			new_keyboard(server, device);
			break;
		case WLR_INPUT_DEVICE_POINTER:
			new_pointer(server, device);
			break;
		default:
			break;
	}

	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

void request_cursor_notify(struct wl_listener* listener, void* data) {
	magpie_server_t* server = wl_container_of(listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event* event =
		static_cast<struct wlr_seat_pointer_request_set_cursor_event*>(data);
	struct wlr_seat_client* focused_client = server->seat->pointer_state.focused_client;

	if (focused_client == event->seat_client) {
		/* Once we've vetted the client, we can tell the cursor to use the
		 * provided surface as the cursor image. It will set the hardware cursor
		 * on the output that it's currently on and continue to do so as the
		 * cursor moves between outputs. */
		wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
	}
}

void seat_request_set_selection(struct wl_listener* listener, void* data) {
	/* This event is raised by the seat when a client wants to set the selection,
	 * usually when the user copies something. wlroots allows compositors to
	 * ignore such requests if they so choose, but in magpie we always honor
	 */
	magpie_server_t* server = wl_container_of(listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event* event = static_cast<struct wlr_seat_request_set_selection_event*>(data);
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

void cursor_axis_notify(struct wl_listener* listener, void* data) {
	/* This event is forwarded by the cursor when a pointer emits an axis event,
	 * for example when you move the scroll wheel. */
	magpie_server_t* server = wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event* event = static_cast<struct wlr_pointer_axis_event*>(data);
	/* Notify the client with pointer focus of the axis event. */
	wlr_seat_pointer_notify_axis(
		server->seat, event->time_msec, event->orientation, event->delta, event->delta_discrete, event->source);
}

void cursor_frame_notify(struct wl_listener* listener, void* data) {
	(void) data;

	/* This event is forwarded by the cursor when a pointer emits an frame
	 * event. Frame events are sent after regular pointer events to group
	 * multiple events together. For instance, two axis events may happen at the
	 * same time, in which case a frame event won't be sent in between. */
	magpie_server_t* server = wl_container_of(listener, server, cursor_frame);
	/* Notify the client with pointer focus of the frame event. */
	wlr_seat_pointer_notify_frame(server->seat);
}

void cursor_motion_absolute_notify(struct wl_listener* listener, void* data) {
	/* This event is forwarded by the cursor when a pointer emits an _absolute_
	 * motion event, from 0..1 on each axis. This happens, for example, when
	 * wlroots is running under a Wayland window rather than KMS+DRM, and you
	 * move the mouse over the window. You could enter the window from any edge,
	 * so we have to warp the mouse there. There is also some hardware which
	 * emits these events. */
	magpie_server_t* server = wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event* event = static_cast<struct wlr_pointer_motion_absolute_event*>(data);
	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
	process_cursor_motion(server, event->time_msec);
}

void cursor_button_notify(struct wl_listener* listener, void* data) {
	/* This event is forwarded by the cursor when a pointer emits a button event.
	 */
	magpie_server_t* server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event* event = static_cast<struct wlr_pointer_button_event*>(data);
	/* Notify the client with pointer focus that a button press has occurred */
	wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
	double sx, sy;
	struct wlr_surface* surface = NULL;
	magpie_surface_t* magpie_surface = surface_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	if (event->state == WLR_BUTTON_RELEASED) {
		/* If you released any buttons, we exit interactive move/resize mode. */
		if (server->cursor_mode != MAGPIE_CURSOR_PASSTHROUGH) {
			reset_cursor_mode(server);
		}
	} else if (magpie_surface != NULL && magpie_surface->type == MAGPIE_SURFACE_TYPE_VIEW) {
		/* Focus that client if the button was _pressed_ */
		focus_view(magpie_surface->view, surface);
	}
}

void cursor_motion_notify(struct wl_listener* listener, void* data) {
	/* This event is forwarded by the cursor when a pointer emits a _relative_
	 * pointer motion event (i.e. a delta) */
	magpie_server_t* server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event* event = static_cast<struct wlr_pointer_motion_event*>(data);

	/* The cursor doesn't move unless we tell it to. The cursor automatically
	 * handles constraining the motion to the output layout, as well as any
	 * special configuration applied for the specific input device which
	 * generated the event. You can pass NULL for the device if you want to move
	 * the cursor around without any input. */
	wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
	process_cursor_motion(server, event->time_msec);
}
