#include "server.hpp"

#include "input/seat.hpp"
#include "output.hpp"
#include "surface/layer.hpp"
#include "surface/popup.hpp"
#include "surface/surface.hpp"
#include "surface/view.hpp"
#include "types.hpp"
#include "xwayland.hpp"

#include <utility>

#include "wlr-wrap-start.hpp"
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_security_context_v1.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_foreign_registry.h>
#include <wlr/types/wlr_xdg_foreign_v1.h>
#include <wlr/types/wlr_xdg_foreign_v2.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>
#include <wlr/xwayland/shell.h>
#include "wlr-wrap-end.hpp"

static wlr_layer_surface_v1* find_subsurface_parent_layer(const wlr_subsurface* subsurface) {
	wlr_surface* parent = subsurface->parent;
	wlr_subsurface* parent_as_subsurface = wlr_subsurface_try_from_wlr_surface(parent);
	wlr_layer_surface_v1* parent_as_layer_surface = wlr_layer_surface_v1_try_from_wlr_surface(parent);

	// traverse up the tree to find the root parent surface
	while (parent_as_subsurface != nullptr) {
		parent = parent_as_subsurface->parent;
		parent_as_subsurface = wlr_subsurface_try_from_wlr_surface(parent);
		parent_as_layer_surface = wlr_layer_surface_v1_try_from_wlr_surface(parent);
	}

	return parent_as_layer_surface;
}

void Server::focus_view(std::shared_ptr<View>&& view) {
	auto layer = this->focused_layer.lock();
	if (layer != nullptr) {
		if (layer->wlr.current.keyboard_interactive != ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE) {
			// if we have a focused layer and it's not exclusive, give focus to the view instead
			focused_layer.reset();
		} else {
			// we shouldn't apply constraints or keyboard focus, we're focused on an exclusive layer
			return;
		}
	}

	std::shared_ptr<View> prev_view = focused_view.lock();
	if (view == prev_view) {
		// Don't re-focus an already focused view, or clear focus if we already don't have it.
		return;
	}

	if (prev_view != nullptr) {
		focused_view.reset();
		prev_view->set_activated(false);
	}

	// if we're just clearing focus, we're done!
	if (view == nullptr) {
		return;
	}

	// Move the view to the front
	views.remove(view);
	views.insert(views.begin(), view);
	focused_view = view;
	view->set_activated(true);
}

void Server::try_focus_next_exclusive_layer() {
	std::shared_ptr<Layer> topmost_exclusive_layer = nullptr;

	// find the topmost layer in exclusive focus mode. if there are multiple within a single scene layer, the spec defines that
	// focus order within that scene layer is implementation defined, so it doesn't matter which is chosen
	for (const auto& output : outputs) {
		for (const auto& layer : output->layers) {
			if (layer->wlr.current.keyboard_interactive == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE &&
				(topmost_exclusive_layer == nullptr || layer->scene_layer > topmost_exclusive_layer->scene_layer)) {
				topmost_exclusive_layer = layer;
			}
		}
	}

	focus_layer(topmost_exclusive_layer);
}

void Server::focus_layer(std::shared_ptr<Layer> layer) {
	if (layer == nullptr) {
		focused_layer.reset();
		if (focused_view.lock() != nullptr) {
			focused_view.lock()->set_activated(true);
		}
		return;
	}

	// if there's already an exclusive focused shell layer with an equal or higher scene layer, just return
	auto focused_layer_locked = focused_layer.lock();
	if (focused_layer_locked != nullptr &&
		focused_layer_locked->wlr.current.keyboard_interactive == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE &&
		focused_layer_locked->scene_layer >= layer->scene_layer) {
		return;
	}

	// same if this layer can't gain focus
	if (layer->wlr.current.keyboard_interactive == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) {
		return;
	}

	focused_layer = layer;

	const auto* keyboard = wlr_seat_get_keyboard(seat->wlr);
	if (keyboard != nullptr) {
		wlr_seat_keyboard_notify_enter(
			seat->wlr, layer->get_wlr_surface(), keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
	}
}

std::weak_ptr<Surface> Server::surface_at(const double lx, const double ly, wlr_surface** wlr, double* sx, double* sy) const {
	/* This returns the topmost node in the scene at the given layout coords.
	 * we only care about surface nodes as we are specifically looking for a
	 * surface in the surface tree of a magpie_view. */
	wlr_scene_node* node = wlr_scene_node_at(&scene->tree.node, lx, ly, sx, sy);
	if (node == nullptr || node->type != WLR_SCENE_NODE_BUFFER) {
		return {};
	}
	wlr_scene_buffer* scene_buffer = wlr_scene_buffer_from_node(node);
	const wlr_scene_surface* scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
	if (scene_surface == nullptr) {
		return {};
	}

	*wlr = scene_surface->surface;
	/* Find the node corresponding to the magpie_view at the root of this
	 * surface tree, it is the only one for which we set the data field. */
	const wlr_scene_tree* tree = node->parent;
	while (tree != nullptr && tree->node.data == nullptr) {
		tree = tree->node.parent;
	}

	if (tree != nullptr) {
		return static_cast<Surface*>(tree->node.data)->weak_from_this();
	}

	return {};
}

/* This event is raised by the backend when a new output (aka a display or
 * monitor) becomes available. */
static void new_output_notify(wl_listener* listener, void* data) {
	wlr_log(WLR_DEBUG, "wlr_backend.events.new_output(listener=%p, data=%p)", (void*) listener, data);

	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_backend.events.new_output");
		return;
	}

	Server& server = magpie_container_of(listener, server, backend_new_output);
	auto* new_output = static_cast<wlr_output*>(data);

	if (server.drm_manager != nullptr) {
		wlr_drm_lease_v1_manager_offer_output(server.drm_manager, new_output);
	}

	/* Configures the output created by the backend to use our allocator
	 * and our renderer. Must be done once, before commiting the output */
	wlr_output_init_render(new_output, server.allocator, server.renderer);

	/* Some backends don't have modes. DRM+KMS does, and we need to set a mode
	 * before we can use the output. The mode is a tuple of (width, height,
	 * refresh rate), and each monitor supports only a specific set of modes. We
	 * just pick the monitor's preferred mode, a more sophisticated compositor
	 * would let the user configure it. */
	if (wl_list_empty(&new_output->modes) == 0) {
		wlr_output_mode* mode = wlr_output_preferred_mode(new_output);
		wlr_output_state state = {};
		wlr_output_state_init(&state);
		wlr_output_state_set_mode(&state, mode);
		wlr_output_state_set_enabled(&state, true);
		if (!wlr_output_commit_state(new_output, &state)) {
			wlr_log(WLR_ERROR, "Failed to commit mode to new output %s", new_output->name);
			wlr_output_state_finish(&state);
			return;
		}

		wlr_output_state_finish(&state);
	}

	/* Allocates and configures our state for this output */
	auto output = std::make_shared<Output>(server, *new_output);
	server.outputs.emplace(output);

	/* Adds this to the output layout. The add_auto function arranges outputs
	 * from left-to-right in the order they appear. A more sophisticated
	 * compositor would let the user configure the arrangement of outputs in the
	 * layout.
	 *
	 * The output layout utility automatically adds a wl_output global to the
	 * display, which Wayland clients can see to find out information about the
	 * output (such as DPI, scale factor, manufacturer, etc).
	 */
	wlr_output_layout_add_auto(server.output_layout, new_output);

	output->update_layout();
}

static void output_power_manager_set_mode_notify([[maybe_unused]] wl_listener* listener, void* data) {
	wlr_log(WLR_DEBUG, "wlr_output_power_manager.events.set_mode(listener=%p, data=%p)", (void*) listener, data);

	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_output_power_manager.events.set_mode");
		return;
	}

	const auto& event = *static_cast<wlr_output_power_v1_set_mode_event*>(data);

	wlr_output_state state = {};
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, event.mode == ZWLR_OUTPUT_POWER_V1_MODE_ON);
	if (!wlr_output_commit_state(event.output, &state)) {
		wlr_log(WLR_ERROR, "Failed to set enabled state %d for output %s", state.enabled, event.output->name);
	}
	wlr_output_state_finish(&state);
}

static void new_xdg_toplevel_notify(wl_listener* listener, void* data) {
	wlr_log(WLR_DEBUG, "wlr_xdg_shell.events.new_toplevel(listener=%p, data=%p)", (void*) listener, data);

	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_xdg_shell.events.new_toplevel");
		return;
	}

	Server& server = magpie_container_of(listener, server, xdg_shell_new_xdg_toplevel);
	auto& xdg_toplevel = *static_cast<wlr_xdg_toplevel*>(data);

	server.views.emplace_back(std::make_shared<XdgView>(server, xdg_toplevel));
}

static void new_xdg_popup_notify(wl_listener*, void* data) {
	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_xdg_shell.events.new_popup");
		return;
	}

	auto& xdg_popup = *static_cast<wlr_xdg_popup*>(data);

	auto* parent = wlr_xdg_surface_try_from_wlr_surface(xdg_popup.parent);

	auto& parent_surface = *static_cast<Surface*>(parent->data);
	parent_surface.popups.emplace(std::make_shared<Popup>(parent_surface, xdg_popup));
}

static void new_layer_surface_notify(wl_listener* listener, void* data) {
	wlr_log(WLR_DEBUG, "wlr_layer_shell_v1.events.new_surface(listener=%p, data=%p)", (void*) listener, data);

	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_layer_shell_v1.events.new_surface");
		return;
	}

	Server& server = magpie_container_of(listener, server, layer_shell_new_layer_surface);
	auto& layer_surface = *static_cast<wlr_layer_surface_v1*>(data);

	/* Allocate a View for this surface */
	Output* output;
	if (layer_surface.output == nullptr) {
		output = static_cast<Output*>(wlr_output_layout_get_center_output(server.output_layout)->data);
		layer_surface.output = &output->wlr;
	} else {
		output = static_cast<Output*>(layer_surface.output->data);
	}

	output->layers.emplace(std::make_shared<Layer>(*output, layer_surface));
}

static void request_activation_notify(wl_listener* listener, void* data) {
	wlr_log(WLR_DEBUG, "wlr_xdg_activation_v1.events.request_activation(listener=%p, data=%p)", (void*) listener, data);

	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_xdg_activation_v1.events.request_activation");
		return;
	}

	Server& server = magpie_container_of(listener, server, activation_request_activation);
	const auto* event = static_cast<wlr_xdg_activation_v1_request_activate_event*>(data);

	const auto* xdg_surface = wlr_xdg_surface_try_from_wlr_surface(event->surface);
	if (xdg_surface != nullptr) {
		auto* view = dynamic_cast<View*>(static_cast<Surface*>(xdg_surface->surface->data));
		if (view != nullptr && xdg_surface->surface->mapped) {
			server.focus_view(std::dynamic_pointer_cast<View>(view->shared_from_this()));
		}
		return;
	}

	const auto* layer_surface = wlr_layer_surface_v1_try_from_wlr_surface(event->surface);
	if (layer_surface != nullptr) {
		auto* layer = dynamic_cast<Layer*>(static_cast<Surface*>(layer_surface->surface->data));
		if (layer != nullptr && layer_surface->surface->mapped) {
			server.focus_layer(std::dynamic_pointer_cast<Layer>(layer->shared_from_this()));
		}
		return;
	}

	const auto* subsurface = wlr_subsurface_try_from_wlr_surface(event->surface);
	if (subsurface != nullptr) {
		wlr_layer_surface_v1* parent_as_layer_surface = find_subsurface_parent_layer(subsurface);
		if (parent_as_layer_surface != nullptr) {
			auto* layer = dynamic_cast<Layer*>(static_cast<Surface*>(layer_surface->surface->data));
			if (layer != nullptr && parent_as_layer_surface->surface->mapped) {
				server.focus_layer(std::dynamic_pointer_cast<Layer>(layer->shared_from_this()));
			}
		}
	}
}

static void drm_lease_request_notify(wl_listener* listener, void* data) {
	wlr_log(WLR_DEBUG, "wlr_drm_lease_manager_v1.events.drm_lease_request(listener=%p, data=%p)", (void*) listener, data);

	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_drm_lease_manager_v1.events.drm_lease_request");
		return;
	}

	Server& server = magpie_container_of(listener, server, drm_lease_request);
	auto* request = static_cast<wlr_drm_lease_request_v1*>(data);

	const wlr_drm_lease_v1* lease = wlr_drm_lease_request_v1_grant(request);
	if (lease == nullptr) {
		wlr_drm_lease_request_v1_reject(request);
		return;
	}

	for (size_t i = 0; i < request->n_connectors; i++) {
		auto* output = static_cast<Output*>(request->connectors[i]->output->data);
		if (output == nullptr) {
			continue;
		}

		wlr_output_state state = {};
		wlr_output_state_init(&state);
		wlr_output_state_set_enabled(&state, false);
		wlr_output_commit_state(&output->wlr, &state);
		wlr_output_state_finish(&state);
		wlr_output_layout_remove(server.output_layout, &output->wlr);
		output->is_leased = true;
	}
}

void output_layout_change_notify(wl_listener* listener, [[maybe_unused]] void* data) {
	wlr_log(WLR_DEBUG, "wlr_output_manager.events.change(listener=%p, data=%p)", (void*) listener, data);

	Server& server = magpie_container_of(listener, server, output_layout_change);

	if (server.num_pending_output_layout_changes > 0) {
		return;
	}

	wlr_output_configuration_v1* config = wlr_output_configuration_v1_create();

	for (const auto& output : server.outputs) {
		wlr_output_configuration_head_v1* head = wlr_output_configuration_head_v1_create(config, &output->wlr);

		wlr_box box = {};
		wlr_output_layout_get_box(server.output_layout, &output->wlr, &box);
		if (!wlr_box_empty(&box)) {
			head->state.x = box.x;
			head->state.y = box.y;
		}
	}

	wlr_output_manager_v1_set_configuration(server.output_manager, config);
}

void output_manager_apply_notify(wl_listener* listener, void* data) {
	wlr_log(WLR_DEBUG, "wlr_output_manager_v1.events.apply(listener=%p, data=%p)", (void*) listener, data);

	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_output_manager_v1.events.apply");
		return;
	}

	Server& server = magpie_container_of(listener, server, output_manager_apply);
	auto& config = *static_cast<wlr_output_configuration_v1*>(data);

	server.num_pending_output_layout_changes++;

	wlr_output_configuration_head_v1* head;
	wl_list_for_each(head, &config.heads, link) {
		Output& output = *static_cast<Output*>(head->state.output->data);
		const bool enabled = head->state.enabled && !output.is_leased;
		const bool adding = enabled && !output.wlr.enabled;
		const bool removing = !enabled && output.wlr.enabled;

		wlr_output_state state = {};
		wlr_output_state_init(&state);
		wlr_output_state_set_enabled(&state, enabled);

		if (enabled) {
			if (head->state.mode != nullptr) {
				wlr_output_state_set_mode(&state, head->state.mode);
			} else {
				const int32_t width = head->state.custom_mode.width;
				const int32_t height = head->state.custom_mode.height;
				const int32_t refresh = head->state.custom_mode.refresh;
				wlr_output_state_set_custom_mode(&state, width, height, refresh);
			}

			wlr_output_state_set_scale(&state, head->state.scale);
			wlr_output_state_set_transform(&state, head->state.transform);
		}

		if (!wlr_output_commit_state(&output.wlr, &state)) {
			wlr_log(WLR_ERROR, "Output config commit failed");
			continue;
		}
		wlr_output_state_finish(&state);

		if (adding) {
			wlr_output_layout_add_auto(server.output_layout, &output.wlr);
		}

		if (enabled) {
			wlr_box box = {};
			wlr_output_layout_get_box(server.output_layout, &output.wlr, &box);
			if (box.x != head->state.x || box.y != head->state.y) {
				/* This overrides the automatic layout */
				wlr_output_layout_add(server.output_layout, &output.wlr, head->state.x, head->state.y);
			}
		}

		if (removing) {
			wlr_output_layout_remove(server.output_layout, &output.wlr);
		}
	}

	wlr_output_configuration_v1_send_succeeded(&config);
	wlr_output_configuration_v1_destroy(&config);

	for (const auto& output : server.outputs) {
		wlr_xcursor_manager_load(server.seat->cursor.cursor_mgr, output->wlr.scale);
	}

	server.seat->cursor.reload_image();
}

bool filter_globals(const struct wl_client* client, const struct wl_global* global, void* data) {
	const auto& server = *static_cast<Server*>(data);

	if (server.xwayland != nullptr) {
		const auto* wlr_xwayland = server.xwayland->wlr;

		if (global == wlr_xwayland->shell_v1->global) {
			return wlr_xwayland->server != nullptr && client == wlr_xwayland->server->client;
		}
	}

	const auto* security_context =
		wlr_security_context_manager_v1_lookup_client(server.security_context_manager, (wl_client*) client);
	if (server.is_restricted(global)) {
		return security_context == nullptr;
	}

	return true;
}

void early_exit(wl_display* display, const std::string& err) {
	wlr_log(WLR_ERROR, "%s", err.c_str());
	wl_display_destroy_clients(display);
	wl_display_destroy(display);
	std::exit(2);
}

Server::Server() : listeners(*this) {
	/* The Wayland display is managed by libwayland. It handles accepting
	 * clients from the Unix socket, manging Wayland globals, and so on. */
	display = wl_display_create();
	if (display == nullptr) {
		wlr_log(WLR_ERROR, "Failed to create a wl_display");
		std::exit(2);
	}

	/* The backend is a wlroots feature which abstracts the underlying input and
	 * output hardware. The autocreate option will choose the most suitable
	 * backend based on the current environment, such as opening an X11 window
	 * if an X11 server is running. */
	session = nullptr;
	backend = wlr_backend_autocreate(wl_display_get_event_loop(display), &session);
	if (backend == nullptr) {
		early_exit(display, "Failed to create a wlr_backend for the Wayland display");
	}

	/* Autocreates a renderer, either Pixman, GLES2 or Vulkan for us. The user
	 * can also specify a renderer using the WLR_RENDERER env var.
	 * The renderer is responsible for defining the various pixel formats it
	 * supports for shared memory, this configures that for clients. */
	renderer = wlr_renderer_autocreate(backend);
	if (renderer == nullptr) {
		early_exit(display, "Failed to create a wlr_renderer for the Wayland display");
	}
	wlr_renderer_init_wl_display(renderer, display);

	/* Autocreates an allocator for us.
	 * The allocator is the bridge between the renderer and the backend. It
	 * handles the buffer creation, allowing wlroots to render onto the
	 * screen */
	allocator = wlr_allocator_autocreate(backend, renderer);
	if (allocator == nullptr) {
		early_exit(display, "Failed to create a wlr_allocator for the Wayland display");
	}

	/* This creates some hands-off wlroots interfaces. The compositor is
	 * necessary for clients to allocate surfaces, the subcompositor allows to
	 * assign the role of subsurfaces to surfaces and the data device manager
	 * handles the clipboard. Each of these wlroots interfaces has room for you
	 * to dig your fingers in and play with their behavior if you want. Note that
	 * the clients cannot set the selection directly without compositor approval,
	 * see the handling of the request_set_selection event below.*/
	compositor = wlr_compositor_create(display, 6, renderer);
	wlr_subcompositor_create(display);
	wlr_data_device_manager_create(display);

	security_context_manager = wlr_security_context_manager_v1_create(display);
	wl_display_set_global_filter(display, filter_globals, this);

	// https://wayfire.org/2020/08/04/Wayfire-0-5.html
	wlr_primary_selection_v1_device_manager_create(display);

	/* Creates an output layout, which a wlroots utility for working with an
	 * arrangement of screens in a physical layout. */
	output_layout = wlr_output_layout_create(display);
	listeners.output_layout_change.notify = output_layout_change_notify;
	wl_signal_add(&output_layout->events.change, &listeners.output_layout_change);

	wlr_xdg_output_manager_v1_create(display, output_layout);

	output_manager = wlr_output_manager_v1_create(display);
	listeners.output_manager_apply.notify = output_manager_apply_notify;
	wl_signal_add(&output_manager->events.apply, &listeners.output_manager_apply);

	output_power_manager = wlr_output_power_manager_v1_create(display);
	listeners.output_power_manager_set_mode.notify = output_power_manager_set_mode_notify;
	wl_signal_add(&output_power_manager->events.set_mode, &listeners.output_power_manager_set_mode);

	seat = std::make_shared<Seat>(*this);

	/* Configure a listener to be notified when new outputs are available on the
	 * backend. */
	listeners.backend_new_output.notify = new_output_notify;
	wl_signal_add(&backend->events.new_output, &listeners.backend_new_output);

	/* Create a scene graph. This is a wlroots abstraction that handles all
	 * rendering and damage tracking. All the compositor author needs to do
	 * is add things that should be rendered to the scene graph at the proper
	 * positions and then call wlr_scene_output_commit() to render a frame if
	 * necessary.
	 */
	scene = wlr_scene_create();
	if (scene == nullptr) {
		early_exit(display, "Failed to create a wlr_scene for the Wayland display");
	}

	for (int32_t idx = 0; idx <= MAGPIE_SCENE_LAYER_LOCK; idx++) {
		scene_layers[idx] = wlr_scene_tree_create(&scene->tree);
		wlr_scene_node_raise_to_top(&scene_layers[idx]->node);
	}

	scene_layout = wlr_scene_attach_output_layout(scene, output_layout);

	auto* presentation = wlr_presentation_create(display, backend);
	if (presentation == nullptr) {
		early_exit(display, "Failed to create a wlr_presentation for the Wayland display");
	}

	xdg_shell = wlr_xdg_shell_create(display, 5);
	listeners.xdg_shell_new_xdg_toplevel.notify = new_xdg_toplevel_notify;
	wl_signal_add(&xdg_shell->events.new_toplevel, &listeners.xdg_shell_new_xdg_toplevel);
	listeners.xdg_shell_new_xdg_popup.notify = new_xdg_popup_notify;
	wl_signal_add(&xdg_shell->events.new_popup, &listeners.xdg_shell_new_xdg_popup);

	layer_shell = wlr_layer_shell_v1_create(display, 4);
	listeners.layer_shell_new_layer_surface.notify = new_layer_surface_notify;
	wl_signal_add(&layer_shell->events.new_surface, &listeners.layer_shell_new_layer_surface);

	xdg_activation = wlr_xdg_activation_v1_create(display);
	listeners.activation_request_activation.notify = request_activation_notify;
	wl_signal_add(&xdg_activation->events.request_activate, &listeners.activation_request_activation);

	data_control_manager = wlr_data_control_manager_v1_create(display);
	foreign_toplevel_manager = wlr_foreign_toplevel_manager_v1_create(display);
	wlr_fractional_scale_manager_v1_create(display, 1);

	xwayland = std::make_shared<XWayland>(*this);

	wlr_viewporter_create(display);
	wlr_single_pixel_buffer_manager_v1_create(display);
	screencopy_manager = wlr_screencopy_manager_v1_create(display);
	export_dmabuf_manager = wlr_export_dmabuf_manager_v1_create(display);
	gamma_control_manager = wlr_gamma_control_manager_v1_create(display);

	wlr_xdg_foreign_registry* foreign_registry = wlr_xdg_foreign_registry_create(display);
	wlr_xdg_foreign_v1_create(display, foreign_registry);
	wlr_xdg_foreign_v2_create(display, foreign_registry);

	idle_notifier = wlr_idle_notifier_v1_create(display);
	idle_inhibit_manager = wlr_idle_inhibit_v1_create(display);

	drm_manager = wlr_drm_lease_v1_manager_create(display, backend);
	if (drm_manager != nullptr) {
		listeners.drm_lease_request.notify = drm_lease_request_notify;
		wl_signal_add(&drm_manager->events.request, &listeners.drm_lease_request);
	}

	content_type_manager = wlr_content_type_manager_v1_create(display, 1);
}

bool Server::is_restricted(const wl_global* global) const {
	if (drm_manager != nullptr) {
		wlr_drm_lease_device_v1* drm_lease_dev;
		wl_list_for_each(drm_lease_dev, &drm_manager->devices, link) {
			if (global == drm_lease_dev->global) {
				return true;
			}
		}
	}

	// clang-format off
	return
		global == data_control_manager->global ||
		global == foreign_toplevel_manager->global ||
		global == export_dmabuf_manager->global ||
		global == gamma_control_manager->global ||
		global == layer_shell->global ||
		global == output_manager->global ||
		global == output_power_manager->global ||
		global == seat->virtual_keyboard_mgr->global ||
		global == seat->virtual_pointer_mgr->global ||
		global == screencopy_manager->global ||
		global == security_context_manager->global;
	// clang-format on
}
