#ifndef MAGPIE_XWAYLAND_H
#define MAGPIE_XWAYLAND_H

#include "types.hpp"
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

class XWayland {
  private:
	struct xwayland_listener_container* listeners;

  public:
	magpie_server_t* server;
	struct wlr_xwayland* wlr_xwayland;
	xcb_atom_t atoms[ATOM_LAST];

	XWayland(magpie_server_t* server);

	static XWayland& from_listener(wl_listener listener);
};

struct xwayland_listener_container {
	XWayland* parent;
	wl_listener ready;
	wl_listener new_surface;
};

#endif
