#ifndef MAGPIE_SERVER_HPP
#define MAGPIE_SERVER_HPP

#include "input.hpp"
#include "surface.hpp"
#include "types.hpp"
#include "xwayland.hpp"

#include <set>
#include <vector>
#include <wayland-server-core.h>

#include "wlr-wrap-start.hpp"
#include <wlr/util/box.h>
#include "wlr-wrap-end.hpp"

typedef enum {
	MAGPIE_SCENE_LAYER_BACKGROUND = 0,
	MAGPIE_SCENE_LAYER_BOTTOM,
	MAGPIE_SCENE_LAYER_NORMAL,
	MAGPIE_SCENE_LAYER_TOP,
	MAGPIE_SCENE_LAYER_OVERLAY,
	MAGPIE_SCENE_LAYER_LOCK,
	MAGPIE_SCENE_LAYER_MAX
} magpie_scene_layer_t;

struct server_listener_container {
	Server* parent;
	wl_listener xdg_shell_new_xdg_surface;
	wl_listener layer_shell_new_layer_surface;
	wl_listener activation_request_activation;
	wl_listener cursor_motion;
	wl_listener cursor_motion_absolute;
	wl_listener cursor_button;
	wl_listener cursor_axis;
	wl_listener cursor_frame;
	wl_listener seat_new_input;
	wl_listener seat_request_cursor;
	wl_listener seat_request_set_selection;
	wl_listener backend_new_output;
};

class Server {
  private:
	server_listener_container listeners;

  public:
	wl_display* display;
	struct wlr_backend* backend;
	struct wlr_renderer* renderer;
	struct wlr_allocator* allocator;
	struct wlr_compositor* compositor;

	XWayland* xwayland;

	struct wlr_scene* scene;
	struct wlr_scene_tree* scene_layers[MAGPIE_SCENE_LAYER_MAX];

	struct wlr_xdg_shell* xdg_shell;

	wl_list views;

	struct wlr_xdg_activation_v1* xdg_activation;

	struct wlr_layer_shell_v1* layer_shell;
	wl_list layers;

	struct wlr_cursor* cursor;
	struct wlr_xcursor_manager* cursor_mgr;

	struct wlr_seat* seat;

	std::vector<Keyboard*> keyboards;
	magpie_cursor_mode_t cursor_mode;
	magpie_view_t* grabbed_view;
	double grab_x, grab_y;
	struct wlr_box grab_geobox;
	uint32_t resize_edges;

	struct wlr_xdg_output_manager_v1* output_manager;
	struct wlr_output_layout* output_layout;
	std::set<Output*> outputs;

	struct wlr_idle_notifier_v1* idle_notifier;
	struct wlr_idle_inhibit_manager_v1* idle_inhibit_manager;

	Server();

	magpie_surface_t* surface_at(double lx, double ly, struct wlr_surface** surface, double* sx, double* sy);

	void focus_view(magpie_view_t* view, struct wlr_surface* surface);
};

#endif
