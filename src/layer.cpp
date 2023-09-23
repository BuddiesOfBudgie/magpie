#include "layer.hpp"

#include "output.hpp"
#include "popup.hpp"
#include "server.hpp"
#include "surface.hpp"
#include "types.hpp"

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

static void subsurface_destroy_notify(wl_listener* listener, void* data) {
	LayerSubsurface& subsurface = *magpie_container_of(listener, subsurface, destroy);
	(void) data;

	subsurface.parent.subsurfaces.erase(&subsurface);
	delete &subsurface;
}

LayerSubsurface::LayerSubsurface(Layer& parent, wlr_subsurface* subsurface) noexcept : parent(parent) {
	listeners.parent = this;

	this->subsurface = subsurface;

	listeners.destroy.notify = subsurface_destroy_notify;
	wl_signal_add(&subsurface->events.destroy, &listeners.destroy);
}

LayerSubsurface::~LayerSubsurface() noexcept {
	wl_list_remove(&listeners.destroy.link);
}

/* Called when the surface is mapped, or ready to display on-screen. */
static void wlr_layer_surface_v1_map_notify(wl_listener* listener, void* data) {
	Layer& layer = *magpie_container_of(listener, layer, map);
	(void) data;

	wlr_scene_node_set_enabled(&layer.scene_layer_surface->tree->node, true);
}

/* Called when the surface is unmapped, and should no longer be shown. */
static void wlr_layer_surface_v1_unmap_notify(wl_listener* listener, void* data) {
	Layer& layer = *magpie_container_of(listener, layer, unmap);
	(void) data;

	wlr_scene_node_set_enabled(&layer.scene_layer_surface->tree->node, false);
}

/* Called when the surface is destroyed and should never be shown again. */
static void wlr_layer_surface_v1_destroy_notify(wl_listener* listener, void* data) {
	Layer& layer = *magpie_container_of(listener, layer, destroy);
	(void) data;

	layer.output.layers.erase(&layer);
	delete &layer;
}

static void wlr_layer_surface_v1_commit_notify(wl_listener* listener, void* data) {
	Layer& layer = *magpie_container_of(listener, layer, commit);
	(void) data;

	Server& server = layer.output.server;
	wlr_layer_surface_v1* surface = layer.layer_surface;

	uint32_t committed = surface->current.committed;
	if (committed & WLR_LAYER_SURFACE_V1_STATE_LAYER) {
		magpie_scene_layer_t chosen_layer = magpie_layer_from_wlr_layer(surface->current.layer);
		wlr_scene_node_reparent(&layer.scene_layer_surface->tree->node, server.scene_layers[chosen_layer]);
	}

	if (committed) {
		layer.output.update_layout();
	}
}

static void wlr_layer_surface_v1_new_popup_notify(wl_listener* listener, void* data) {
	Layer& layer = *magpie_container_of(listener, layer, new_popup);
	auto* surface = static_cast<Surface*>(layer.layer_surface->surface->data);

	new Popup(*surface, static_cast<wlr_xdg_popup*>(data));
}

static void wlr_layer_surface_v1_new_subsurface_notify(wl_listener* listener, void* data) {
	Layer& layer = *magpie_container_of(listener, layer, new_subsurface);
	auto* subsurface = static_cast<wlr_subsurface*>(data);

	layer.subsurfaces.emplace(new LayerSubsurface(layer, subsurface));
}

Layer::Layer(Output& output, wlr_layer_surface_v1* surface) noexcept : output(output) {
	listeners.parent = this;

	layer_surface = surface;

	magpie_scene_layer_t chosen_layer = magpie_layer_from_wlr_layer(surface->current.layer);
	scene_layer_surface = wlr_scene_layer_surface_v1_create(output.server.scene_layers[chosen_layer], surface);

	Surface* magpie_surface = new Surface(*this);
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
}

Layer::~Layer() noexcept {
	wl_list_remove(&listeners.map.link);
	wl_list_remove(&listeners.unmap.link);
	wl_list_remove(&listeners.destroy.link);
	wl_list_remove(&listeners.commit.link);
	wl_list_remove(&listeners.new_popup.link);
	wl_list_remove(&listeners.new_subsurface.link);
}
