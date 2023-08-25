#include "surface.hpp"
#include "layer.hpp"
#include "popup.hpp"
#include "types.hpp"
#include "view.hpp"

#include <cstdlib>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_scene.h>
#include "wlr-wrap-end.hpp"

magpie_surface_t* new_magpie_surface_from_view(magpie_view_t* view) {
	magpie_surface_t* surface = (magpie_surface_t*) std::calloc(1, sizeof(magpie_surface_t));
	surface->type = MAGPIE_SURFACE_TYPE_VIEW;
	surface->server = view->server;
	surface->view = view;
	surface->scene_tree = view->scene_tree;
	return surface;
}

magpie_surface_t* new_magpie_surface_from_layer(magpie_layer_t* layer) {
	magpie_surface_t* surface = (magpie_surface_t*) std::calloc(1, sizeof(magpie_surface_t));
	surface->type = MAGPIE_SURFACE_TYPE_LAYER;
	surface->server = layer->server;
	surface->layer = layer;
	surface->scene_tree = layer->scene_layer_surface->tree;
	return surface;
}

magpie_surface_t* new_magpie_surface_from_popup(magpie_popup_t* popup) {
	magpie_surface_t* surface = (magpie_surface_t*) std::calloc(1, sizeof(magpie_surface_t));
	surface->type = MAGPIE_SURFACE_TYPE_POPUP;
	surface->server = popup->server;
	surface->popup = popup;
	surface->scene_tree = popup->scene_tree;
	return surface;
}
