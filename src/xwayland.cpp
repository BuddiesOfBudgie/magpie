#define _POSIX_C_SOURCE 200809L

#include "xwayland.hpp"
#include "server.hpp"
#include "types.hpp"
#include "view.hpp"

#include "wlr-wrap-start.hpp"
#include <wlr/util/log.h>
#include <wlr/xwayland.h>
#include "wlr-wrap-end.hpp"

static const char* atom_map[ATOM_LAST] = {
	"_NET_WM_WINDOW_TYPE_NORMAL",
	"_NET_WM_WINDOW_TYPE_DIALOG",
	"_NET_WM_WINDOW_TYPE_UTILITY",
	"_NET_WM_WINDOW_TYPE_TOOLBAR",
	"_NET_WM_WINDOW_TYPE_SPLASH",
	"_NET_WM_WINDOW_TYPE_MENU",
	"_NET_WM_WINDOW_TYPE_DROPDOWN_MENU",
	"_NET_WM_WINDOW_TYPE_POPUP_MENU",
	"_NET_WM_WINDOW_TYPE_TOOLTIP",
	"_NET_WM_WINDOW_TYPE_NOTIFICATION",
	"_NET_WM_STATE_MODAL",
};

static void ready_notify(wl_listener* listener, void* data) {
	(void) data;

	xwayland_listener_container* container = wl_container_of(listener, container, ready);
	XWayland& xwayland = *container->parent;

	wlr_xwayland_set_seat(xwayland.wlr_xwayland, xwayland.server->seat);

	xcb_connection_t* xcb_conn = xcb_connect(NULL, NULL);
	int err = xcb_connection_has_error(xcb_conn);
	if (err) {
		wlr_log(WLR_ERROR, "XCB connect failed: %d", err);
		return;
	}

	xcb_intern_atom_cookie_t cookies[ATOM_LAST];
	for (size_t i = 0; i < ATOM_LAST; i++) {
		cookies[i] = xcb_intern_atom(xcb_conn, 0, strlen(atom_map[i]), atom_map[i]);
	}
	for (size_t i = 0; i < ATOM_LAST; i++) {
		xcb_generic_error_t* error = NULL;
		xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(xcb_conn, cookies[i], &error);
		if (reply != NULL && error == NULL) {
			xwayland.atoms[i] = reply->atom;
		}
		free(reply);

		if (error != NULL) {
			wlr_log(WLR_ERROR, "could not resolve atom %s: X11 error code %d", atom_map[i], error->error_code);
			free(error);
			break;
		}
	}

	xcb_disconnect(xcb_conn);
}

static void new_surface_notify(wl_listener* listener, void* data) {
	xwayland_listener_container* container = wl_container_of(listener, container, new_surface);
	XWayland& xwayland = *container->parent;

	struct wlr_xwayland_surface* xwayland_surface = static_cast<struct wlr_xwayland_surface*>(data);

	new_magpie_xwayland_view(xwayland.server, xwayland_surface);
}

XWayland::XWayland(magpie_server_t* server) {
	listeners = {};
	listeners->parent = this;

	this->server = server;
	wlr_xwayland = wlr_xwayland_create(server->display, server->compositor, true);

	listeners->ready.notify = ready_notify;
	wl_signal_add(&wlr_xwayland->events.ready, &listeners->ready);
	listeners->new_surface.notify = new_surface_notify;
	wl_signal_add(&wlr_xwayland->events.new_surface, &listeners->new_surface);

	setenv("DISPLAY", wlr_xwayland->display_name, true);
}
