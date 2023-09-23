#include "surface.hpp"

#include "layer.hpp"
#include "output.hpp"
#include "popup.hpp"
#include "view.hpp"

Surface::Surface(View& view) noexcept : server(view.get_server()), type(MAGPIE_SURFACE_TYPE_VIEW) {
	scene_node = view.scene_node;
	this->view = &view;
}

Surface::Surface(Layer& layer) noexcept : server(layer.output.server), type(MAGPIE_SURFACE_TYPE_LAYER) {
	scene_node = &layer.scene_layer_surface->tree->node;
	this->layer = &layer;
}

Surface::Surface(Popup& popup) noexcept : server(popup.server), type(MAGPIE_SURFACE_TYPE_POPUP) {
	scene_node = popup.scene_node;
	this->popup = &popup;
}
