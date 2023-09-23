#include "surface.hpp"

#include "layer.hpp"
#include "output.hpp"
#include "popup.hpp"
#include "view.hpp"

Surface::Surface(View& view) noexcept : server(view.get_server()), type(MAGPIE_SURFACE_TYPE_VIEW), view(view) {
	scene_node = view.scene_node;
}

Surface::Surface(Layer& layer) noexcept : server(layer.output.server), type(MAGPIE_SURFACE_TYPE_LAYER), layer(layer) {
	scene_node = &layer.scene_layer_surface->tree->node;
}

Surface::Surface(Popup& popup) noexcept : server(popup.server), type(MAGPIE_SURFACE_TYPE_POPUP), popup(popup) {
	scene_node = popup.scene_node;
}
