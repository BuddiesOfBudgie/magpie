#ifndef MAGPIE_SERVER_HPP
#define MAGPIE_SERVER_HPP

#include "types.hpp"
#include "xwayland.hpp"

#include <list>
#include <set>
#include <vector>

#include "wlr-wrap-start.hpp"
#include <wayland-server.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_idle.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/xwayland.h>
#include "wlr-wrap-end.hpp"

typedef enum {
	MAGPIE_SCENE_LAYER_BACKGROUND = 0,
	MAGPIE_SCENE_LAYER_BOTTOM,
	MAGPIE_SCENE_LAYER_NORMAL,
	MAGPIE_SCENE_LAYER_TOP,
	MAGPIE_SCENE_LAYER_OVERLAY,
	MAGPIE_SCENE_LAYER_LOCK
} magpie_scene_layer_t;

class Server {
  public:
	struct Listeners {
		std::reference_wrapper<Server> parent;
		wl_listener xdg_shell_new_xdg_surface;
		wl_listener layer_shell_new_layer_surface;
		wl_listener activation_request_activation;
		wl_listener backend_new_input;
		wl_listener backend_new_output;
		Listeners(Server& parent) noexcept : parent(std::ref(parent)) {}
	};

  private:
	Listeners listeners;

  public:
	wl_display* display;
	wlr_backend* backend;
	wlr_renderer* renderer;
	wlr_allocator* allocator;
	wlr_compositor* compositor;

	XWayland* xwayland;

	wlr_scene* scene;
	wlr_scene_tree* scene_layers[MAGPIE_SCENE_LAYER_LOCK + 1];

	wlr_xdg_shell* xdg_shell;

	wlr_xdg_activation_v1* xdg_activation;

	wlr_data_control_manager_v1* data_controller;
	wlr_foreign_toplevel_manager_v1* foreign_toplevel_manager;

	wlr_layer_shell_v1* layer_shell;

	Seat* seat;

	std::list<View*> views;
	View* grabbed_view;
	double grab_x, grab_y;
	wlr_box grab_geobox;
	uint32_t resize_edges;

	wlr_xdg_output_manager_v1* output_manager;
	wlr_output_layout* output_layout;
	std::set<Output*> outputs;

	wlr_idle_notifier_v1* idle_notifier;
	wlr_idle_inhibit_manager_v1* idle_inhibit_manager;

	Server();

	Surface* surface_at(const double lx, const double ly, wlr_surface** surface, double* sx, double* sy);
	void focus_view(View& view, wlr_surface* surface);
};

#endif
