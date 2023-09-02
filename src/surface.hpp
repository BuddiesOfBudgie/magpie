#ifndef MAGPIE_SURFACE_HPP
#define MAGPIE_SURFACE_HPP

#include "types.hpp"

typedef enum { MAGPIE_SURFACE_TYPE_VIEW, MAGPIE_SURFACE_TYPE_LAYER, MAGPIE_SURFACE_TYPE_POPUP } magpie_surface_type_t;

struct magpie_surface {
	Server* server;

	magpie_surface_type_t type;
	struct wlr_scene_tree* scene_tree;

	union {
		View* view;
		Layer* layer;
		Popup* popup;
	};
};

magpie_surface_t* new_magpie_surface_from_view(View& view);
magpie_surface_t* new_magpie_surface_from_layer(Layer& view);
magpie_surface_t* new_magpie_surface_from_popup(Popup& popup);

#endif
