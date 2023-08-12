#include "surface.h"
#include "popup.h"
#include "types.h"
#include "view.h"
#include <stdlib.h>

magpie_surface_t* new_magpie_surface_from_view(magpie_view_t* view) {
    magpie_surface_t* surface = calloc(1, sizeof(magpie_surface_t));
    surface->type = MAGPIE_SURFACE_TYPE_VIEW;
    surface->server = view->server;
    surface->view = view;
    surface->scene_tree = view->scene_tree;
    return surface;
}

magpie_surface_t* new_magpie_surface_from_popup(magpie_popup_t* popup) {
    magpie_surface_t* surface = calloc(1, sizeof(magpie_surface_t));
    surface->type = MAGPIE_SURFACE_TYPE_POPUP;
    surface->server = popup->server;
    surface->popup = popup;
    surface->scene_tree = popup->scene_tree;
    return surface;
}
