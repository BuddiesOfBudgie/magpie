#include "popup.h"
#include "surface.h"
#include "types.h"
#include <stdlib.h>

static void popup_map_notify(struct wl_listener* listener, void* data) {
    (void) listener;
    (void) data;
}

static void popup_unmap_notify(struct wl_listener* listener, void* data) {
    (void) listener;
    (void) data;
}

static void popup_destroy_notify(struct wl_listener* listener, void* data) {
    (void) data;

    magpie_popup_t* popup = wl_container_of(listener, popup, destroy);
    wl_list_remove(&popup->map.link);
    wl_list_remove(&popup->unmap.link);
    wl_list_remove(&popup->destroy.link);
    wl_list_remove(&popup->commit.link);
    wl_list_remove(&popup->new_popup.link);

    free(popup);
}

static void popup_commit_notify(struct wl_listener* listener, void* data) {
    (void) listener;
    (void) data;
}

static void popup_new_popup_notify(struct wl_listener* listener, void* data);

magpie_popup_t* new_magpie_popup(magpie_surface_t* parent_surface, struct wlr_xdg_popup* xdg_popup) {
    magpie_popup_t* popup = calloc(1, sizeof(magpie_popup_t));
    popup->server = parent_surface->server;
    popup->xdg_popup = xdg_popup;
    popup->parent = parent_surface;
    popup->scene_tree = wlr_scene_xdg_surface_create(parent_surface->scene_tree, xdg_popup->base);
    
    magpie_surface_t* surface = new_magpie_surface_from_popup(popup);
    popup->scene_tree->node.data = surface;
    xdg_popup->base->surface->data = surface;

    popup->map.notify = popup_map_notify;
    wl_signal_add(&xdg_popup->base->events.map, &popup->map);
    popup->unmap.notify = popup_unmap_notify;
    wl_signal_add(&xdg_popup->base->events.unmap, &popup->unmap);
    popup->destroy.notify = popup_destroy_notify;
    wl_signal_add(&xdg_popup->base->events.destroy, &popup->destroy);
    popup->commit.notify = popup_commit_notify;
    wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);
    popup->new_popup.notify = popup_new_popup_notify;
    wl_signal_add(&xdg_popup->base->events.new_popup, &popup->new_popup);
    
    return popup;
}

static void popup_new_popup_notify(struct wl_listener* listener, void* data) {
    magpie_popup_t* popup = wl_container_of(listener, popup, new_popup);
    new_magpie_popup(popup->parent, data);
}
