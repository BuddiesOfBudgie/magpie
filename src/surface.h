#ifndef MAGPIE_SURFACE_H
#define MAGPIE_SURFACE_H

#include "types.h"

typedef enum { MAGPIE_SURFACE_TYPE_VIEW, MAGPIE_SURFACE_TYPE_POPUP } magpie_surface_type_t;

struct magpie_surface {
    magpie_server_t* server;

    magpie_surface_type_t type;
    struct wlr_scene_tree* scene_tree;

    union {
        magpie_view_t* view;
        magpie_popup_t* popup;
    };
};

magpie_surface_t* new_magpie_surface_from_view(magpie_view_t* view);
magpie_surface_t* new_magpie_surface_from_popup(magpie_popup_t* popup);

#endif