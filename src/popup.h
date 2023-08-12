#ifndef MAGPIE_POPUP_H
#define MAGPIE_POPUP_H

#include "surface.h"
#include "types.h"
#include <wayland-server-core.h>

struct magpie_popup {
	magpie_server_t* server;

	magpie_surface_t* parent;

	struct wlr_xdg_popup* xdg_popup;
	struct wlr_scene_tree* scene_tree;

	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener commit;
	struct wl_listener new_popup;
};

magpie_popup_t* new_magpie_popup(magpie_surface_t* parent_surface, struct wlr_xdg_popup* xdg_popup);

#endif
