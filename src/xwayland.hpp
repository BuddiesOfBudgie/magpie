#ifndef MAGPIE_XWAYLAND_H
#define MAGPIE_XWAYLAND_H

#include "types.hpp"

#include <functional>
#include <xcb/xproto.h>

#include "wlr-wrap-start.hpp"
#include <wlr/xwayland/xwayland.h>
#include "wlr-wrap-end.hpp"

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
  public:
	struct Listeners {
		std::reference_wrapper<XWayland> parent;
		wl_listener ready;
		wl_listener new_surface;
		Listeners(XWayland& parent) noexcept : parent(parent) {}
	};

  private:
	Listeners listeners;

  public:
	Server& server;
	wlr_xwayland* xwayland;
	xcb_atom_t atoms[ATOM_LAST];

	XWayland(Server& server) noexcept;
};

#endif
