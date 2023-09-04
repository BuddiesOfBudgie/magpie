#ifndef MAGPIE_POPUP_HPP
#define MAGPIE_POPUP_HPP

#include "surface.hpp"
#include "types.hpp"

#include <wayland-server-core.h>

struct popup_listener_container {
	Popup* parent;
	wl_listener map;
	wl_listener unmap;
	wl_listener destroy;
	wl_listener commit;
	wl_listener new_popup;
};

class Popup {
  private:
	popup_listener_container listeners;

  public:
	Server& server;
	magpie_surface_t& parent;

	struct wlr_xdg_popup* xdg_popup;
	struct wlr_scene_node* scene_node;

	Popup(magpie_surface_t& parent, struct wlr_xdg_popup* xdg_popup);
};

#endif
