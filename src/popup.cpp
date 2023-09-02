#include "popup.hpp"
#include "surface.hpp"
#include "types.hpp"

#include <cstdlib>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "wlr-wrap-end.hpp"

static void popup_map_notify(wl_listener* listener, void* data) {
	(void) listener;
	(void) data;
}

static void popup_unmap_notify(wl_listener* listener, void* data) {
	(void) listener;
	(void) data;
}

static void popup_destroy_notify(wl_listener* listener, void* data) {
	(void) data;

	popup_listener_container* container = wl_container_of(listener, container, destroy);
	Popup& popup = *container->parent;
	wl_list_remove(&container->map.link);
	wl_list_remove(&container->unmap.link);
	wl_list_remove(&container->destroy.link);
	wl_list_remove(&container->commit.link);
	wl_list_remove(&container->new_popup.link);

	delete &popup;
}

static void popup_commit_notify(wl_listener* listener, void* data) {
	(void) listener;
	(void) data;
}

static void popup_new_popup_notify(wl_listener* listener, void* data) {
	popup_listener_container* container = wl_container_of(listener, container, new_popup);
	new Popup(container->parent->parent, static_cast<wlr_xdg_popup*>(data));
}

Popup::Popup(magpie_surface_t& parent_surface, struct wlr_xdg_popup* xdg_popup)
	: server(*parent_surface.server), parent(parent_surface) {
	listeners.parent = this;

	this->xdg_popup = xdg_popup;
	scene_tree = wlr_scene_xdg_surface_create(parent_surface.scene_tree, xdg_popup->base);

	magpie_surface_t* surface = new_magpie_surface_from_popup(*this);
	scene_tree->node.data = surface;
	xdg_popup->base->surface->data = surface;

	listeners.map.notify = popup_map_notify;
	wl_signal_add(&xdg_popup->base->events.map, &listeners.map);
	listeners.unmap.notify = popup_unmap_notify;
	wl_signal_add(&xdg_popup->base->events.unmap, &listeners.unmap);
	listeners.destroy.notify = popup_destroy_notify;
	wl_signal_add(&xdg_popup->base->events.destroy, &listeners.destroy);
	listeners.commit.notify = popup_commit_notify;
	wl_signal_add(&xdg_popup->base->surface->events.commit, &listeners.commit);
	listeners.new_popup.notify = popup_new_popup_notify;
	wl_signal_add(&xdg_popup->base->events.new_popup, &listeners.new_popup);
}
