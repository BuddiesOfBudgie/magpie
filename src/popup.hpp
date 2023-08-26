#ifndef MAGPIE_POPUP_HPP
#define MAGPIE_POPUP_HPP

#include "surface.hpp"
#include "types.hpp"

#include <wayland-server-core.h>

struct magpie_popup {
	Server* server;

	magpie_surface_t* parent;

	struct wlr_xdg_popup* xdg_popup;
	struct wlr_scene_tree* scene_tree;

	wl_listener map;
	wl_listener unmap;
	wl_listener destroy;
	wl_listener commit;
	wl_listener new_popup;
};

magpie_popup_t* new_magpie_popup(magpie_surface_t* parent_surface, struct wlr_xdg_popup* xdg_popup);

#endif
