#ifndef MAGPIE_SERVER_HPP
#define MAGPIE_SERVER_HPP

#include "types.hpp"

#include <functional>
#include <list>
#include <memory>
#include <set>

#include "wlr-wrap-start.hpp"
#include <wlr/backend/session.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_content_type_v1.h>
#include <wlr/types/wlr_drm_lease_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "wlr-wrap-end.hpp"

typedef enum {
	MAGPIE_SCENE_LAYER_BACKGROUND = 0,
	MAGPIE_SCENE_LAYER_BOTTOM,
	MAGPIE_SCENE_LAYER_NORMAL,
	MAGPIE_SCENE_LAYER_TOP,
	MAGPIE_SCENE_LAYER_OVERLAY,
	MAGPIE_SCENE_LAYER_LOCK
} magpie_scene_layer_t;

class Server final : public std::enable_shared_from_this<Server> {
  public:
	struct Listeners {
		std::reference_wrapper<Server> parent;
		wl_listener xdg_shell_new_xdg_surface = {};
		wl_listener layer_shell_new_layer_surface = {};
		wl_listener activation_request_activation = {};
		wl_listener backend_new_output = {};
		wl_listener drm_lease_request = {};
		wl_listener output_layout_change = {};
		wl_listener output_manager_apply = {};
		wl_listener output_power_manager_set_mode = {};
		explicit Listeners(Server& parent) noexcept : parent(parent) {}
	};

  private:
	Listeners listeners;

  public:
	wl_display* display;
	wlr_session* session;
	wlr_backend* backend;
	wlr_renderer* renderer;
	wlr_allocator* allocator;
	wlr_compositor* compositor;

	std::shared_ptr<XWayland> xwayland;

	wlr_scene* scene;
	wlr_scene_output_layout* scene_layout;
	wlr_scene_tree* scene_layers[MAGPIE_SCENE_LAYER_LOCK + 1] = {};

	wlr_xdg_shell* xdg_shell;

	wlr_xdg_activation_v1* xdg_activation;

	wlr_foreign_toplevel_manager_v1* foreign_toplevel_manager;

	wlr_layer_shell_v1* layer_shell;

	std::shared_ptr<Seat> seat;

	std::list<std::shared_ptr<View>> views;
	std::weak_ptr<View> focused_view;
	std::weak_ptr<View> grabbed_view;
	double grab_x = 0.0, grab_y = 0.0;
	wlr_box grab_geobox = {};
	uint32_t resize_edges = 0;

	wlr_output_manager_v1* output_manager;
	wlr_output_power_manager_v1* output_power_manager;
	wlr_output_layout* output_layout;
	std::set<std::shared_ptr<Output>> outputs;
	uint8_t num_pending_output_layout_changes = 0;

	wlr_idle_notifier_v1* idle_notifier;
	wlr_idle_inhibit_manager_v1* idle_inhibit_manager;

	wlr_drm_lease_v1_manager* drm_manager;
	wlr_content_type_manager_v1* content_type_manager;

	Server();

	std::weak_ptr<Surface> surface_at(double lx, double ly, wlr_surface** wlr, double* sx, double* sy) const;
	void focus_view(std::shared_ptr<View>&& view, wlr_surface* surface = nullptr);
};

#endif
