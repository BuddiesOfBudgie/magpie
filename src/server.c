#include "server.h"
#include "input.h"
#include "output.h"
#include "popup.h"
#include "surface.h"
#include "types.h"
#include "view.h"
#include "xwayland.h"

#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_output_layout.h>

#define WLR_USE_UNSTABLE
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_idle.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/xwayland.h>

void focus_view(magpie_view_t* view, struct wlr_surface* surface) {
    /* Note: this function only deals with keyboard focus. */
    if (view == NULL) {
        return;
    }

    magpie_server_t* server = view->server;
    struct wlr_seat* seat = server->seat;
    struct wlr_surface* prev_surface = seat->keyboard_state.focused_surface;
    if (prev_surface == surface) {
        /* Don't re-focus an already focused surface. */
        return;
    }

    if (prev_surface) {
        struct wlr_surface* previous = seat->keyboard_state.focused_surface;

        if (wlr_surface_is_xdg_surface(previous)) {
            struct wlr_xdg_surface* xdg_previous = wlr_xdg_surface_from_wlr_surface(previous);
            assert(xdg_previous->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);
            wlr_xdg_toplevel_set_activated(xdg_previous->toplevel, false);
        } else if (wlr_surface_is_xwayland_surface(previous)) {
            struct wlr_xwayland_surface* xwayland_previous = wlr_xwayland_surface_from_wlr_surface(previous);
            wlr_xwayland_surface_activate(xwayland_previous, false);
        }
    }

    struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
    /* Move the view to the front */
    wlr_scene_node_raise_to_top(&view->scene_tree->node);
    wl_list_remove(&view->link);
    wl_list_insert(&server->views, &view->link);

    /* Activate the new surface */
    if (view->type == MAGPIE_VIEW_TYPE_XDG) {
        wlr_xdg_toplevel_set_activated(view->xdg_view->xdg_toplevel, true);
    } else {
        wlr_xwayland_surface_activate(view->xwayland_view->xwayland_surface, true);
        wlr_xwayland_surface_restack(view->xwayland_view->xwayland_surface, NULL, XCB_STACK_MODE_ABOVE);
    }

    /*
     * Tell the seat to have the keyboard enter this surface. wlroots will keep
     * track of this and automatically send key events to the appropriate
     * clients without additional work on your part.
     */
    if (keyboard != NULL) {
        wlr_seat_keyboard_notify_enter(
            seat, view->surface, keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
    }
}

magpie_surface_t* surface_at(
    magpie_server_t* server, double lx, double ly, struct wlr_surface** surface, double* sx, double* sy) {
    /* This returns the topmost node in the scene at the given layout coords.
     * we only care about surface nodes as we are specifically looking for a
     * surface in the surface tree of a magpie_view. */
    struct wlr_scene_node* node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
    if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
        return NULL;
    }
    struct wlr_scene_buffer* scene_buffer = wlr_scene_buffer_from_node(node);
    struct wlr_scene_surface* scene_surface = wlr_scene_surface_from_buffer(scene_buffer);
    if (!scene_surface) {
        return NULL;
    }

    *surface = scene_surface->surface;
    /* Find the node corresponding to the magpie_view at the root of this
     * surface tree, it is the only one for which we set the data field. */
    struct wlr_scene_tree* tree = node->parent;
    while (tree != NULL && tree->node.data == NULL) {
        tree = tree->node.parent;
    }
    return tree->node.data;
}

static void new_output_notify(struct wl_listener* listener, void* data) {
    /* This event is raised by the backend when a new output (aka a display or
     * monitor) becomes available. */
    magpie_server_t* server = wl_container_of(listener, server, new_output);
    struct wlr_output* wlr_output = data;

    /* Configures the output created by the backend to use our allocator
     * and our renderer. Must be done once, before commiting the output */
    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    /* Some backends don't have modes. DRM+KMS does, and we need to set a mode
     * before we can use the output. The mode is a tuple of (width, height,
     * refresh rate), and each monitor supports only a specific set of modes. We
     * just pick the monitor's preferred mode, a more sophisticated compositor
     * would let the user configure it. */
    if (!wl_list_empty(&wlr_output->modes)) {
        struct wlr_output_mode* mode = wlr_output_preferred_mode(wlr_output);
        wlr_output_set_mode(wlr_output, mode);
        wlr_output_enable(wlr_output, true);
        if (!wlr_output_commit(wlr_output)) {
            return;
        }
    }

    /* Allocates and configures our state for this output */
    magpie_output_t* output = new_magpie_output(server, wlr_output);
    wl_list_insert(&server->outputs, &output->link);

    /* Adds this to the output layout. The add_auto function arranges outputs
     * from left-to-right in the order they appear. A more sophisticated
     * compositor would let the user configure the arrangement of outputs in the
     * layout.
     *
     * The output layout utility automatically adds a wl_output global to the
     * display, which Wayland clients can see to find out information about the
     * output (such as DPI, scale factor, manufacturer, etc).
     */
    wlr_output_layout_add_auto(server->output_layout, wlr_output);

    magpie_output_update_areas(output);
}

static void new_xdg_surface_notify(struct wl_listener* listener, void* data) {
    /* This event is raised when wlr_xdg_shell receives a new xdg surface from a
     * client, either a toplevel (application window) or popup. */
    magpie_server_t* server = wl_container_of(listener, server, new_xdg_surface);
    struct wlr_xdg_surface* xdg_surface = data;

    if (xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
        new_magpie_xdg_view(server, xdg_surface->toplevel);
    } else if (xdg_surface->role == WLR_XDG_SURFACE_ROLE_POPUP) {
        magpie_surface_t* surface = xdg_surface->popup->parent->data;
        new_magpie_popup(surface, xdg_surface->popup);
    }
}

static void request_activation_notify(struct wl_listener* listener, void* data) {
    magpie_server_t* server = wl_container_of(listener, server, request_activation);
    struct wlr_xdg_activation_v1_request_activate_event* event = data;

    if (!wlr_surface_is_xdg_surface(event->surface)) {
        return;
    }

    struct wlr_xdg_surface *xdg_surface = wlr_xdg_surface_from_wlr_surface(event->surface);
    magpie_surface_t* surface = xdg_surface->surface->data;
    if (surface->type != MAGPIE_SURFACE_TYPE_VIEW || !xdg_surface->mapped) {
        return;
    }

    focus_view(surface->view, xdg_surface->surface);
}

magpie_server_t* new_magpie_server(void) {
    magpie_server_t* server = calloc(1, sizeof(magpie_server_t));

    /* The Wayland display is managed by libwayland. It handles accepting
     * clients from the Unix socket, manging Wayland globals, and so on. */
    server->wl_display = wl_display_create();
    assert(server->wl_display);

    /* The backend is a wlroots feature which abstracts the underlying input and
     * output hardware. The autocreate option will choose the most suitable
     * backend based on the current environment, such as opening an X11 window
     * if an X11 server is running. */
    server->backend = wlr_backend_autocreate(server->wl_display);
    assert(server->backend);

    /* Autocreates a renderer, either Pixman, GLES2 or Vulkan for us. The user
     * can also specify a renderer using the WLR_RENDERER env var.
     * The renderer is responsible for defining the various pixel formats it
     * supports for shared memory, this configures that for clients. */
    server->renderer = wlr_renderer_autocreate(server->backend);
    assert(server->renderer);
    wlr_renderer_init_wl_display(server->renderer, server->wl_display);

    /* Autocreates an allocator for us.
     * The allocator is the bridge between the renderer and the backend. It
     * handles the buffer creation, allowing wlroots to render onto the
     * screen */
    server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
    assert(server->allocator);

    /* This creates some hands-off wlroots interfaces. The compositor is
     * necessary for clients to allocate surfaces, the subcompositor allows to
     * assign the role of subsurfaces to surfaces and the data device manager
     * handles the clipboard. Each of these wlroots interfaces has room for you
     * to dig your fingers in and play with their behavior if you want. Note that
     * the clients cannot set the selection directly without compositor approval,
     * see the handling of the request_set_selection event below.*/
    server->compositor = wlr_compositor_create(server->wl_display, server->renderer);
    wlr_subcompositor_create(server->wl_display);
    wlr_data_device_manager_create(server->wl_display);

    /* Creates an output layout, which a wlroots utility for working with an
     * arrangement of screens in a physical layout. */
    server->output_layout = wlr_output_layout_create();
    assert(server->output_layout);

    server->output_manager = wlr_xdg_output_manager_v1_create(server->wl_display, server->output_layout);

    /* Configure a listener to be notified when new outputs are available on the
     * backend. */
    wl_list_init(&server->outputs);
    server->new_output.notify = new_output_notify;
    wl_signal_add(&server->backend->events.new_output, &server->new_output);

    /* Create a scene graph. This is a wlroots abstraction that handles all
     * rendering and damage tracking. All the compositor author needs to do
     * is add things that should be rendered to the scene graph at the proper
     * positions and then call wlr_scene_output_commit() to render a frame if
     * necessary.
     */
    server->scene = wlr_scene_create();
    assert(server->scene);
    wlr_scene_attach_output_layout(server->scene, server->output_layout);

    /* Set up xdg-shell version 3. The xdg-shell is a Wayland protocol which is
     * used for application windows. For more detail on shells, refer to my
     * article:
     *
     * https://drewdevault.com/2018/07/29/Wayland-shells.html
     */
    wl_list_init(&server->views);
    server->xdg_shell = wlr_xdg_shell_create(server->wl_display, 5);
    server->new_xdg_surface.notify = new_xdg_surface_notify;
    wl_signal_add(&server->xdg_shell->events.new_surface, &server->new_xdg_surface);

    server->xdg_activation = wlr_xdg_activation_v1_create(server->wl_display);
    server->request_activation.notify = request_activation_notify;
    wl_signal_add(&server->xdg_activation->events.request_activate, &server->request_activation);

    /*
     * Creates a cursor, which is a wlroots utility for tracking the cursor
     * image shown on screen.
     */
    server->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server->cursor, server->output_layout);

    /* Creates an xcursor manager, another wlroots utility which loads up
     * Xcursor themes to source cursor images from and makes sure that cursor
     * images are available at all scale factors on the screen (necessary for
     * HiDPI support). We add a cursor theme at scale factor 1 to begin with. */
    server->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
    wlr_xcursor_manager_load(server->cursor_mgr, 1);

    server->xwayland = new_magpie_xwayland(server);

    /*
     * wlr_cursor *only* displays an image on screen. It does not move around
     * when the pointer moves. However, we can attach input devices to it, and
     * it will generate aggregate events for all of them. In these events, we
     * can choose how we want to process them, forwarding them to clients and
     * moving the cursor around. More detail on this process is described in my
     * input handling blog post:
     *
     * https://drewdevault.com/2018/07/17/Input-handling-in-wlroots.html
     *
     * And more comments are sprinkled throughout the notify functions above.
     */
    server->cursor_mode = MAGPIE_CURSOR_PASSTHROUGH;
    server->cursor_motion.notify = cursor_motion_notify;
    wl_signal_add(&server->cursor->events.motion, &server->cursor_motion);
    server->cursor_motion_absolute.notify = cursor_motion_absolute_notify;
    wl_signal_add(&server->cursor->events.motion_absolute, &server->cursor_motion_absolute);
    server->cursor_button.notify = cursor_button_notify;
    wl_signal_add(&server->cursor->events.button, &server->cursor_button);
    server->cursor_axis.notify = cursor_axis_notify;
    wl_signal_add(&server->cursor->events.axis, &server->cursor_axis);
    server->cursor_frame.notify = cursor_frame_notify;
    wl_signal_add(&server->cursor->events.frame, &server->cursor_frame);

    /*
     * Configures a seat, which is a single "seat" at which a user sits and
     * operates the computer. This conceptually includes up to one keyboard,
     * pointer, touch, and drawing tablet device. We also rig up a listener to
     * let us know when new input devices are available on the backend.
     */
    wl_list_init(&server->keyboards);
    server->new_input.notify = new_input_notify;
    wl_signal_add(&server->backend->events.new_input, &server->new_input);
    server->seat = wlr_seat_create(server->wl_display, "seat0");
    server->request_cursor.notify = request_cursor_notify;
    wl_signal_add(&server->seat->events.request_set_cursor, &server->request_cursor);
    server->request_set_selection.notify = seat_request_set_selection;
    wl_signal_add(&server->seat->events.request_set_selection, &server->request_set_selection);

    wlr_viewporter_create(server->wl_display);
    wlr_single_pixel_buffer_manager_v1_create(server->wl_display);

    server->idle_notifier = wlr_idle_notifier_v1_create(server->wl_display);
    server->idle_inhibit_manager = wlr_idle_inhibit_v1_create(server->wl_display);

    return server;
}