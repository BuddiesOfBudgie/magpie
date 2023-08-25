#ifndef MAGPIE_SURFACE_HPP
#define MAGPIE_SURFACE_HPP

#include "types.hpp"

typedef enum { MAGPIE_SURFACE_TYPE_VIEW, MAGPIE_SURFACE_TYPE_LAYER, MAGPIE_SURFACE_TYPE_POPUP } magpie_surface_type_t;

struct magpie_surface {
	magpie_server_t* server;

	magpie_surface_type_t type;
	struct wlr_scene_tree* scene_tree;

	union {
		magpie_view_t* view;
		magpie_layer_t* layer;
		magpie_popup_t* popup;
	};
};

magpie_surface_t* new_magpie_surface_from_view(magpie_view_t* view);
magpie_surface_t* new_magpie_surface_from_layer(magpie_layer_t* view);
magpie_surface_t* new_magpie_surface_from_popup(magpie_popup_t* popup);

#endif
