#ifndef MAGPIE_XWAYLAND_HPP
#define MAGPIE_XWAYLAND_HPP

#include "types.hpp"

#include <functional>
#include <list>
#include <memory>
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

class XWayland final : std::enable_shared_from_this<XWayland> {
  public:
	struct Listeners {
		std::reference_wrapper<XWayland> parent;
		wl_listener ready = {};
		wl_listener new_surface = {};
		explicit Listeners(XWayland& parent) noexcept : parent(parent) {}
	};

  private:
	Listeners listeners;

  public:
	Server& server;
	wlr_xwayland* wlr;
	std::list<View*> unmapped_views;
	xcb_atom_t atoms[ATOM_LAST] = {};

	explicit XWayland(Server& server) noexcept;
};

#endif
