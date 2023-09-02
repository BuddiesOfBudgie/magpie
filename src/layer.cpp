#include "layer.hpp"
#include "output.hpp"
#include "popup.hpp"
#include "server.hpp"
#include "surface.hpp"
#include "types.hpp"
#include "xdg-shell-protocol.h"

#include <cassert>
#include <cstdlib>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include "wlr-wrap-end.hpp"

static magpie_scene_layer_t magpie_layer_from_wlr_layer(enum zwlr_layer_shell_v1_layer layer) {
	switch (layer) {
		case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
			return MAGPIE_SCENE_LAYER_BACKGROUND;
		case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
			return MAGPIE_SCENE_LAYER_BOTTOM;
		case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
			return MAGPIE_SCENE_LAYER_TOP;
		default:
			return MAGPIE_SCENE_LAYER_OVERLAY;
	}
}

static void update_layer_layout(Server* server) {
	for (auto* output : server->outputs) {
		output->update_areas();
	}

	for (int i = 0; i < MAGPIE_SCENE_LAYER_LOCK; i++) {
		if (i == MAGPIE_SCENE_LAYER_NORMAL) {
			continue;
		}
		struct wlr_scene_tree* layer_tree = server->scene_layers[i];
		if (!layer_tree->node.enabled) {
			continue;
		}

		struct wlr_scene_node* child;
		wl_list_for_each(child, &layer_tree->children, link) {
			if (child->type != WLR_SCENE_NODE_TREE) {
				continue;
			}

			magpie_surface_t* magpie_surface = static_cast<magpie_surface_t*>(child->data);
			if (magpie_surface->type != MAGPIE_SURFACE_TYPE_LAYER) {
				continue;
			}

			Layer& layer = *magpie_surface->layer;
			Output* output = layer.output;
			if (output == nullptr) {
				continue;
			}

			wlr_scene_layer_surface_v1_configure(
				layer.scene_layer_surface, &layer.output->full_area, &layer.output->usable_area);
		}
	}
}

static void subsurface_destroy_notify(wl_listener* listener, void* data) {
	(void) data;

	layer_subsurface_listener_container* container = wl_container_of(listener, container, destroy);
	LayerSubsurface& subsurface = *container->parent;

	wl_list_remove(&container->destroy.link);
	subsurface.parent_layer.subsurfaces.erase(&subsurface);
	delete &subsurface;
}

LayerSubsurface::LayerSubsurface(Layer& parent_layer, struct wlr_subsurface* wlr_subsurface) : parent_layer(parent_layer) {
	listeners.parent = this;

	this->wlr_subsurface = wlr_subsurface;

	listeners.destroy.notify = subsurface_destroy_notify;
	wl_signal_add(&wlr_subsurface->events.destroy, &listeners.destroy);
}

static void wlr_layer_surface_v1_map_notify(wl_listener* listener, void* data) {
	(void) data;

	/* Called when the surface is mapped, or ready to display on-screen. */
	layer_listener_container* container = wl_container_of(listener, container, map);
	Layer& layer = *container->parent;

	layer.server.layers.emplace(&layer);
}

static void wlr_layer_surface_v1_unmap_notify(wl_listener* listener, void* data) {
	(void) data;

	/* Called when the surface is unmapped, and should no longer be shown. */
	layer_listener_container* container = wl_container_of(listener, container, unmap);
	Layer& layer = *container->parent;

	layer.server.layers.erase(&layer);
}

static void wlr_layer_surface_v1_destroy_notify(wl_listener* listener, void* data) {
	(void) data;

	/* Called when the surface is destroyed and should never be shown again. */
	layer_listener_container* container = wl_container_of(listener, container, destroy);
	Layer& layer = *container->parent;

	wl_list_remove(&container->map.link);
	wl_list_remove(&container->unmap.link);
	wl_list_remove(&container->destroy.link);
	wl_list_remove(&container->commit.link);
	wl_list_remove(&container->new_popup.link);
	wl_list_remove(&container->new_subsurface.link);

	layer.server.layers.erase(&layer); // just in case
	delete &layer;
}

static void wlr_layer_surface_v1_commit_notify(wl_listener* listener, void* data) {
	(void) data;

	layer_listener_container* container = wl_container_of(listener, container, commit);
	Layer& layer = *container->parent;

	Server& server = layer.server;
	struct wlr_layer_surface_v1* surface = layer.layer_surface;

	uint32_t committed = surface->current.committed;
	if (committed & WLR_LAYER_SURFACE_V1_STATE_LAYER) {
		magpie_scene_layer_t chosen_layer = magpie_layer_from_wlr_layer(surface->current.layer);
		wlr_scene_node_reparent(&layer.scene_layer_surface->tree->node, server.scene_layers[chosen_layer]);
	}

	if (committed) {
		update_layer_layout(&server);
	}
}

static void wlr_layer_surface_v1_new_popup_notify(wl_listener* listener, void* data) {
	layer_listener_container* container = wl_container_of(listener, container, new_popup);
	Layer& layer = *container->parent;

	magpie_surface_t* surface = static_cast<magpie_surface_t*>(layer.layer_surface->surface->data);
	new Popup(*surface, static_cast<struct wlr_xdg_popup*>(data));
}

static void wlr_layer_surface_v1_new_subsurface_notify(wl_listener* listener, void* data) {
	layer_listener_container* container = wl_container_of(listener, container, new_subsurface);
	Layer& layer = *container->parent;

	struct wlr_subsurface* subsurface = static_cast<struct wlr_subsurface*>(data);
	layer.subsurfaces.emplace(new LayerSubsurface(layer, subsurface));
}

static void wlr_layer_surface_v1_output_destroy_notify(wl_listener* listener, void* data) {
	(void) data;

	layer_listener_container* container = wl_container_of(listener, container, output_destroy);
	Layer& layer = *container->parent;

	layer.layer_surface->output = nullptr;
	wl_list_remove(&container->output_destroy.link);
	wlr_layer_surface_v1_destroy(layer.layer_surface);
}

Layer::Layer(Server& server, struct wlr_layer_surface_v1* surface) : server(server) {
	listeners.parent = this;

	layer_surface = surface;

	if (surface->output == nullptr) {
		Output* output = static_cast<Output*>(wlr_output_layout_get_center_output(server.output_layout)->data);
		surface->output = output->wlr_output;
	}
	output = static_cast<Output*>(surface->output->data);

	magpie_scene_layer_t chosen_layer = magpie_layer_from_wlr_layer(surface->current.layer);
	scene_layer_surface = wlr_scene_layer_surface_v1_create(server.scene_layers[chosen_layer], surface);

	magpie_surface_t* magpie_surface = new_magpie_surface_from_layer(*this);
	scene_layer_surface->tree->node.data = magpie_surface;
	surface->surface->data = magpie_surface;

	listeners.map.notify = wlr_layer_surface_v1_map_notify;
	wl_signal_add(&surface->events.map, &listeners.map);
	listeners.unmap.notify = wlr_layer_surface_v1_unmap_notify;
	wl_signal_add(&surface->events.unmap, &listeners.unmap);
	listeners.destroy.notify = wlr_layer_surface_v1_destroy_notify;
	wl_signal_add(&surface->events.destroy, &listeners.destroy);
	listeners.commit.notify = wlr_layer_surface_v1_commit_notify;
	wl_signal_add(&surface->surface->events.commit, &listeners.commit);
	listeners.new_popup.notify = wlr_layer_surface_v1_new_popup_notify;
	wl_signal_add(&surface->events.new_popup, &listeners.new_popup);
	listeners.new_subsurface.notify = wlr_layer_surface_v1_new_subsurface_notify;
	wl_signal_add(&surface->surface->events.new_subsurface, &listeners.new_subsurface);
	listeners.output_destroy.notify = wlr_layer_surface_v1_output_destroy_notify;
	wl_signal_add(&surface->output->events.destroy, &listeners.output_destroy);
}
