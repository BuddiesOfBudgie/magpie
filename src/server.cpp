#include "server.hpp"

#include "input/seat.hpp"
#include "output.hpp"
#include "surface/layer.hpp"
#include "surface/popup.hpp"
#include "surface/surface.hpp"
#include "surface/view.hpp"
#include "types.hpp"
#include "xwayland.hpp"

#include <algorithm>
#include <cassert>
#include <wayland-server.h>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include "wlr-wrap-end.hpp"

void Server::focus_view(View& view, wlr_surface* surface) {
	Server& server = view.get_server();
	wlr_seat* seat = server.seat->seat;
	wlr_surface* prev_surface = seat->keyboard_state.focused_surface;
	if (prev_surface == surface) {
		/* Don't re-focus an already focused surface. */
		return;
	}

	if (prev_surface) {
		wlr_surface* previous = seat->keyboard_state.focused_surface;

		if (wlr_surface_is_xdg_surface(previous)) {
			wlr_xdg_surface* xdg_previous = wlr_xdg_surface_from_wlr_surface(previous);
			assert(xdg_previous->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);
			wlr_xdg_toplevel_set_activated(xdg_previous->toplevel, false);
		} else if (wlr_surface_is_xwayland_surface(previous)) {
			wlr_xwayland_surface* xwayland_previous = wlr_xwayland_surface_from_wlr_surface(previous);
			wlr_xwayland_surface_activate(xwayland_previous, false);
		}
	}

	/* Move the view to the front */
	wlr_scene_node_raise_to_top(view.scene_node);
	(void) std::remove(server.views.begin(), server.views.end(), &view);
	for (auto* view : server.views) {
		view->set_activated(false);
	}

	/* Activate the new surface */
	server.views.insert(server.views.begin(), &view);
	view.set_activated(true);

	/*
	 * Tell the seat to have the keyboard enter this surface. wlroots will keep
	 * track of this and automatically send key events to the appropriate
	 * clients without additional work on your part.
	 */
	wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
	if (keyboard != nullptr) {
		wlr_seat_keyboard_notify_enter(seat, view.surface, keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
	}
}

Surface* Server::surface_at(const double lx, const double ly, wlr_surface** surface, double* sx, double* sy) {
	/* This returns the topmost node in the scene at the given layout coords.
	 * we only care about surface nodes as we are specifically looking for a
	 * surface in the surface tree of a magpie_view. */
	wlr_scene_node* node = wlr_scene_node_at(&scene->tree.node, lx, ly, sx, sy);
	if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
		return NULL;
	}
	wlr_scene_buffer* scene_buffer = wlr_scene_buffer_from_node(node);
	wlr_scene_surface* scene_surface = wlr_scene_surface_from_buffer(scene_buffer);
	if (!scene_surface) {
		return NULL;
	}

	*surface = scene_surface->surface;
	/* Find the node corresponding to the magpie_view at the root of this
	 * surface tree, it is the only one for which we set the data field. */
	wlr_scene_tree* tree = node->parent;
	while (tree != NULL && tree->node.data == NULL) {
		tree = tree->node.parent;
	}
	return static_cast<Surface*>(tree->node.data);
}

/* This event is raised by the backend when a new output (aka a display or
 * monitor) becomes available. */
static void new_output_notify(wl_listener* listener, void* data) {
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
	if (!wl_list_empty(&new_output->modes)) {
		wlr_output_mode* mode = wlr_output_preferred_mode(new_output);
		wlr_output_set_mode(new_output, mode);
		wlr_output_enable(new_output, true);
		if (!wlr_output_commit(new_output)) {
			return;
		}
	}

	/* Allocates and configures our state for this output */
	Output* output = new Output(server, new_output);
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

static void output_power_manager_set_mode_notify(wl_listener* listener, void* data) {
	(void) listener;
	auto& event = *static_cast<wlr_output_power_v1_set_mode_event*>(data);

	if (event.mode == ZWLR_OUTPUT_POWER_V1_MODE_ON) {
		wlr_output_enable(event.output, true);
		if (!wlr_output_test(event.output)) {
			wlr_output_rollback(event.output);
		}
		wlr_output_commit(event.output);
	} else {
		wlr_output_enable(event.output, false);
		wlr_output_commit(event.output);
	}
}

/* This event is raised when wlr_xdg_shell receives a new xdg surface from a
 * client, either a toplevel (application window) or popup. */
static void new_xdg_surface_notify(wl_listener* listener, void* data) {
	Server& server = magpie_container_of(listener, server, xdg_shell_new_xdg_surface);
	auto& xdg_surface = *static_cast<wlr_xdg_surface*>(data);

	if (xdg_surface.role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
		new XdgView(server, *xdg_surface.toplevel);
	} else if (xdg_surface.role == WLR_XDG_SURFACE_ROLE_POPUP) {
		auto* surface = static_cast<Surface*>(xdg_surface.popup->parent->data);
		new Popup(*surface, xdg_surface.popup);
	}
}

static void new_layer_surface_notify(wl_listener* listener, void* data) {
	Server& server = magpie_container_of(listener, server, layer_shell_new_layer_surface);
	auto& layer_surface = *static_cast<wlr_layer_surface_v1*>(data);

	/* Allocate a View for this surface */
	Output* output;
	if (layer_surface.output == nullptr) {
		output = static_cast<Output*>(wlr_output_layout_get_center_output(server.output_layout)->data);
		layer_surface.output = output->output;
	} else {
		output = static_cast<Output*>(layer_surface.output->data);
	}

	output->layers.emplace(new Layer(*output, layer_surface));
}

static void request_activation_notify(wl_listener* listener, void* data) {
	Server& server = magpie_container_of(listener, server, activation_request_activation);
	auto* event = static_cast<wlr_xdg_activation_v1_request_activate_event*>(data);

	if (!wlr_surface_is_xdg_surface(event->surface)) {
		return;
	}

	wlr_xdg_surface* xdg_surface = wlr_xdg_surface_from_wlr_surface(event->surface);
	auto* view = dynamic_cast<View*>(static_cast<Surface*>(xdg_surface->surface->data));
	if (view != nullptr && xdg_surface->mapped) {
		server.focus_view(*view, xdg_surface->surface);
	}
}

static void drm_lease_notify(wl_listener* listener, void* data) {
	Server& server = magpie_container_of(listener, server, drm_lease_request);
	auto* request = static_cast<wlr_drm_lease_request_v1*>(data);

	wlr_drm_lease_v1* lease = wlr_drm_lease_request_v1_grant(request);
	if (lease == nullptr) {
		wlr_drm_lease_request_v1_reject(request);
		return;
	}

	for (size_t i = 0; i < request->n_connectors; i++) {
		auto* output = static_cast<Output*>(request->connectors[i]->output->data);
		if (output == nullptr)
			continue;

		wlr_output_enable(output->output, false);
		wlr_output_commit(output->output);
		wlr_output_layout_remove(server.output_layout, output->output);
		output->is_leased = true;
		output->scene_output = nullptr;
	}
}

Server::Server() : listeners(*this) {
	/* The Wayland display is managed by libwayland. It handles accepting
	 * clients from the Unix socket, manging Wayland globals, and so on. */
	display = wl_display_create();
	assert(display);

	/* The backend is a wlroots feature which abstracts the underlying input and
	 * output hardware. The autocreate option will choose the most suitable
	 * backend based on the current environment, such as opening an X11 window
	 * if an X11 server is running. */
	backend = wlr_backend_autocreate(display);
	assert(backend);

	/* Autocreates a renderer, either Pixman, GLES2 or Vulkan for us. The user
	 * can also specify a renderer using the WLR_RENDERER env var.
	 * The renderer is responsible for defining the various pixel formats it
	 * supports for shared memory, this configures that for clients. */
	renderer = wlr_renderer_autocreate(backend);
	assert(renderer);
	wlr_renderer_init_wl_display(renderer, display);

	/* Autocreates an allocator for us.
	 * The allocator is the bridge between the renderer and the backend. It
	 * handles the buffer creation, allowing wlroots to render onto the
	 * screen */
	allocator = wlr_allocator_autocreate(backend, renderer);
	assert(allocator);

	/* This creates some hands-off wlroots interfaces. The compositor is
	 * necessary for clients to allocate surfaces, the subcompositor allows to
	 * assign the role of subsurfaces to surfaces and the data device manager
	 * handles the clipboard. Each of these wlroots interfaces has room for you
	 * to dig your fingers in and play with their behavior if you want. Note that
	 * the clients cannot set the selection directly without compositor approval,
	 * see the handling of the request_set_selection event below.*/
	compositor = wlr_compositor_create(display, renderer);
	wlr_subcompositor_create(display);
	wlr_data_device_manager_create(display);

	// https://wayfire.org/2020/08/04/Wayfire-0-5.html
	wlr_primary_selection_v1_device_manager_create(display);

	/* Creates an output layout, which a wlroots utility for working with an
	 * arrangement of screens in a physical layout. */
	output_layout = wlr_output_layout_create();
	assert(output_layout);

	output_manager = wlr_xdg_output_manager_v1_create(display, output_layout);

	output_power_manager = wlr_output_power_manager_v1_create(display);
	listeners.output_power_manager_set_mode.notify = output_power_manager_set_mode_notify;
	wl_signal_add(&output_power_manager->events.set_mode, &listeners.output_power_manager_set_mode);

	seat = new Seat(*this);

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
	assert(scene);
	for (int idx = 0; idx <= MAGPIE_SCENE_LAYER_LOCK; idx++) {
		scene_layers[idx] = wlr_scene_tree_create(&scene->tree);
		wlr_scene_node_raise_to_top(&scene_layers[idx]->node);
	}

	wlr_scene_attach_output_layout(scene, output_layout);

	auto* presentation = wlr_presentation_create(display, backend);
	assert(presentation);
	wlr_scene_set_presentation(scene, presentation);

	/* Set up xdg-shell version 3. The xdg-shell is a Wayland protocol which is
	 * used for application windows. For more detail on shells, refer to my
	 * article:
	 *
	 * https://drewdevault.com/2018/07/29/Wayland-shells.html
	 */
	xdg_shell = wlr_xdg_shell_create(display, 5);
	listeners.xdg_shell_new_xdg_surface.notify = new_xdg_surface_notify;
	wl_signal_add(&xdg_shell->events.new_surface, &listeners.xdg_shell_new_xdg_surface);

	layer_shell = wlr_layer_shell_v1_create(display);
	listeners.layer_shell_new_layer_surface.notify = new_layer_surface_notify;
	wl_signal_add(&layer_shell->events.new_surface, &listeners.layer_shell_new_layer_surface);

	xdg_activation = wlr_xdg_activation_v1_create(display);
	listeners.activation_request_activation.notify = request_activation_notify;
	wl_signal_add(&xdg_activation->events.request_activate, &listeners.activation_request_activation);

	data_controller = wlr_data_control_manager_v1_create(display);
	foreign_toplevel_manager = wlr_foreign_toplevel_manager_v1_create(display);

	xwayland = new XWayland(*this);

	wlr_viewporter_create(display);
	wlr_single_pixel_buffer_manager_v1_create(display);
	wlr_screencopy_manager_v1_create(display);
	wlr_export_dmabuf_manager_v1_create(display);
	wlr_gamma_control_manager_v1_create(display);

	idle_notifier = wlr_idle_notifier_v1_create(display);
	idle_inhibit_manager = wlr_idle_inhibit_v1_create(display);

	drm_manager = wlr_drm_lease_v1_manager_create(display, backend);
	if (drm_manager != nullptr) {
		listeners.drm_lease_request.notify = drm_lease_notify;
		wl_signal_add(&drm_manager->events.request, &listeners.drm_lease_request);
	}
}
