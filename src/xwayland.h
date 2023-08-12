#ifndef MAGPIE_XWAYLAND_H
#define MAGPIE_XWAYLAND_H

#include "types.h"
#include <wayland-server-core.h>
#include <xcb/xproto.h>

enum atom_name {
	NET_WM_WINDOW_TYPE_NORMAL,
	NET_WM_WINDOW_TYPE_DIALOG,
	NET_WM_WINDOW_TYPE_UTILITY,
	NET_WM_WINDOW_TYPE_TOOLBAR,
	NET_WM_WINDOW_TYPE_SPLASH,
	NET_WM_WINDOW_TYPE_MENU,
	NET_WM_WINDOW_TYPE_DROPDOWN_MENU,
	NET_WM_WINDOW_TYPE_POPUP_MENU,
	NET_WM_WINDOW_TYPE_TOOLTIP,
	NET_WM_WINDOW_TYPE_NOTIFICATION,
	NET_WM_STATE_MODAL,
	ATOM_LAST,
};

struct magpie_xwayland {
	magpie_server_t* server;

	struct wlr_xwayland* wlr_xwayland;

	struct wl_listener ready;
	struct wl_listener new_surface;

	xcb_atom_t atoms[ATOM_LAST];
};

magpie_xwayland_t* new_magpie_xwayland(magpie_server_t* server);

#endif
