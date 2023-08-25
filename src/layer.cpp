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
		case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
			return MAGPIE_SCENE_LAYER_OVERLAY;
		default:
			return MAGPIE_SCENE_LAYER_MAX;
	}
}

static void update_layer_layout(magpie_server_t* server) {
	magpie_output_t* output;
	wl_list_for_each(output, &server->outputs, link) {
		magpie_output_update_areas(output);
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

			magpie_layer_t* layer = magpie_surface->layer;
			magpie_output_t* output = layer->output;
			if (output == NULL) {
				continue;
			}

			wlr_scene_layer_surface_v1_configure(
				layer->scene_layer_surface, &layer->output->full_area, &layer->output->usable_area);
		}
	}
}

static void subsurface_destroy_notify(wl_listener* listener, void* data) {
	(void) data;

	magpie_layer_subsurface_t* subsurface = wl_container_of(listener, subsurface, destroy);
	wl_list_remove(&subsurface->destroy.link);
	free(subsurface);
}

static void new_subsurface(magpie_layer_t* parent_layer, struct wlr_subsurface* wlr_subsurface) {
	magpie_layer_subsurface_t* subsurface = (magpie_layer_subsurface_t*) std::calloc(1, sizeof(magpie_layer_subsurface_t));
	assert(parent_layer != NULL);
	assert(wlr_subsurface != NULL);

	subsurface->wlr_subsurface = wlr_subsurface;
	subsurface->parent_layer = parent_layer;

	subsurface->destroy.notify = subsurface_destroy_notify;
	wl_signal_add(&wlr_subsurface->events.destroy, &subsurface->destroy);
}

static void wlr_layer_surface_v1_map_notify(wl_listener* listener, void* data) {
	(void) data;

	/* Called when the surface is mapped, or ready to display on-screen. */
	magpie_layer_t* layer = wl_container_of(listener, layer, map);
	wl_list_insert(&layer->server->layers, &layer->link);
}

static void wlr_layer_surface_v1_unmap_notify(wl_listener* listener, void* data) {
	(void) data;

	/* Called when the surface is unmapped, and should no longer be shown. */
	magpie_layer_t* layer = wl_container_of(listener, layer, unmap);
	wl_list_remove(&layer->link);
}

static void wlr_layer_surface_v1_destroy_notify(wl_listener* listener, void* data) {
	(void) data;

	/* Called when the surface is destroyed and should never be shown again. */
	magpie_layer_t* view = wl_container_of(listener, view, destroy);

	wl_list_remove(&view->map.link);
	wl_list_remove(&view->unmap.link);
	wl_list_remove(&view->destroy.link);
	wl_list_remove(&view->commit.link);
	wl_list_remove(&view->new_popup.link);
	wl_list_remove(&view->new_subsurface.link);

	free(view);
}

static void wlr_layer_surface_v1_commit_notify(wl_listener* listener, void* data) {
	(void) data;

	magpie_layer_t* layer = wl_container_of(listener, layer, commit);
	magpie_server_t* server = layer->server;
	struct wlr_layer_surface_v1* surface = layer->layer_surface;

	uint32_t committed = surface->current.committed;
	if (committed & WLR_LAYER_SURFACE_V1_STATE_LAYER) {
		magpie_scene_layer_t chosen_layer = magpie_layer_from_wlr_layer(surface->current.layer);
		if (chosen_layer == MAGPIE_SCENE_LAYER_MAX) {
			wlr_log(WLR_ERROR, "invalid layer requested: %d", surface->current.layer);
			wl_resource_post_error(
				surface->resource, ZWLR_LAYER_SHELL_V1_ERROR_INVALID_LAYER, "invalid layer %d", surface->current.layer);
		}

		wlr_scene_node_reparent(&layer->scene_layer_surface->tree->node, server->scene_layers[chosen_layer]);
	}

	if (committed) {
		update_layer_layout(server);
	}
}

static void wlr_layer_surface_v1_new_popup_notify(wl_listener* listener, void* data) {
	magpie_layer_t* layer = wl_container_of(listener, layer, new_popup);
	new_magpie_popup(
		static_cast<magpie_surface_t*>(layer->layer_surface->surface->data), static_cast<struct wlr_xdg_popup*>(data));
}

static void wlr_layer_surface_v1_new_subsurface_notify(wl_listener* listener, void* data) {
	magpie_layer_t* layer = wl_container_of(listener, layer, new_popup);
	struct wlr_subsurface* subsurface = static_cast<struct wlr_subsurface*>(data);
	new_subsurface(layer, subsurface);
}

static void wlr_layer_surface_v1_output_destroy_notify(wl_listener* listener, void* data) {
	(void) data;

	magpie_layer_t* layer = wl_container_of(listener, layer, output_destroy);
	layer->layer_surface->output = NULL;
	wl_list_remove(&layer->output_destroy.link);
	wlr_layer_surface_v1_destroy(layer->layer_surface);
}

magpie_layer_t* new_magpie_layer(magpie_server_t* server, struct wlr_layer_surface_v1* surface) {
	/* Allocate a magpie_layer_t for this surface */
	magpie_layer_t* layer = (magpie_layer_t*) std::calloc(1, sizeof(magpie_layer_t));
	wl_list_init(&layer->subsurfaces);
	layer->server = server;
	layer->layer_surface = surface;

	if (surface->output == NULL) {
		magpie_output_t* output = wl_container_of(server->outputs.next, output, link);
		surface->output = output->wlr_output;
	}
	layer->output = static_cast<magpie_output_t*>(surface->output->data);

	magpie_scene_layer_t chosen_layer = magpie_layer_from_wlr_layer(surface->current.layer);
	if (chosen_layer == MAGPIE_SCENE_LAYER_MAX) {
		wlr_log(WLR_ERROR, "invalid layer requested: %d", surface->current.layer);
		wl_resource_post_error(
			surface->resource, ZWLR_LAYER_SHELL_V1_ERROR_INVALID_LAYER, "invalid layer %d", surface->current.layer);
		return NULL;
	}

	layer->scene_layer_surface = wlr_scene_layer_surface_v1_create(server->scene_layers[chosen_layer], surface);

	magpie_surface_t* magpie_surface = new_magpie_surface_from_layer(layer);
	layer->scene_layer_surface->tree->node.data = new_magpie_surface_from_layer(layer);
	surface->surface->data = magpie_surface;

	layer->map.notify = wlr_layer_surface_v1_map_notify;
	wl_signal_add(&surface->events.map, &layer->map);
	layer->unmap.notify = wlr_layer_surface_v1_unmap_notify;
	wl_signal_add(&surface->events.unmap, &layer->unmap);
	layer->destroy.notify = wlr_layer_surface_v1_destroy_notify;
	wl_signal_add(&surface->events.destroy, &layer->destroy);
	layer->commit.notify = wlr_layer_surface_v1_commit_notify;
	wl_signal_add(&surface->surface->events.commit, &layer->commit);
	layer->new_popup.notify = wlr_layer_surface_v1_new_popup_notify;
	wl_signal_add(&surface->events.new_popup, &layer->new_popup);
	layer->new_subsurface.notify = wlr_layer_surface_v1_new_subsurface_notify;
	wl_signal_add(&surface->surface->events.new_subsurface, &layer->new_subsurface);
	layer->output_destroy.notify = wlr_layer_surface_v1_output_destroy_notify;
	wl_signal_add(&surface->output->events.destroy, &layer->output_destroy);

	return layer;
}
