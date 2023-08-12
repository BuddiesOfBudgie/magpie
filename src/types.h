#ifndef MAGPIE_TYPES_H
#define MAGPIE_TYPES_H

#include <assert.h>
#include <wayland-server-core.h>
#include <wlr/util/box.h>

#define WLR_USE_UNSTABLE
#include <wlr/backend.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>

typedef struct magpie_server magpie_server_t;
typedef struct magpie_output magpie_output_t;

typedef struct magpie_xwayland magpie_xwayland_t;

typedef struct magpie_surface magpie_surface_t;
typedef struct magpie_layer magpie_layer_t;
typedef struct magpie_layer_subsurface magpie_layer_subsurface_t;
typedef struct magpie_popup magpie_popup_t;
typedef struct magpie_view magpie_view_t;
typedef struct magpie_xdg_view magpie_xdg_view_t;
typedef struct magpie_xwayland_view magpie_xwayland_view_t;

typedef struct magpie_keyboard magpie_keyboard_t;

#endif
