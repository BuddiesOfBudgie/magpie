#include "surface.hpp"
#include "layer.hpp"
#include "popup.hpp"
#include "types.hpp"
#include "view.hpp"

#include <cstdlib>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_scene.h>
#include "wlr-wrap-end.hpp"

magpie_surface_t* new_magpie_surface_from_view(View& view) {
	magpie_surface_t* surface = (magpie_surface_t*) std::calloc(1, sizeof(magpie_surface_t));
	surface->type = MAGPIE_SURFACE_TYPE_VIEW;
	surface->server = &view.get_server();
	surface->view = &view;
	surface->scene_node = view.scene_node;
	return surface;
}

magpie_surface_t* new_magpie_surface_from_layer(Layer& layer) {
	magpie_surface_t* surface = (magpie_surface_t*) std::calloc(1, sizeof(magpie_surface_t));
	surface->type = MAGPIE_SURFACE_TYPE_LAYER;
	surface->server = &layer.server;
	surface->layer = &layer;
	surface->scene_node = &layer.scene_layer_surface->tree->node;
	return surface;
}

magpie_surface_t* new_magpie_surface_from_popup(Popup& popup) {
	magpie_surface_t* surface = (magpie_surface_t*) std::calloc(1, sizeof(magpie_surface_t));
	surface->type = MAGPIE_SURFACE_TYPE_POPUP;
	surface->server = &popup.server;
	surface->popup = &popup;
	surface->scene_node = popup.scene_node;
	return surface;
}
