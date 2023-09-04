#ifndef FOREIGN_TOPLEVEL_HPP
#define FOREIGN_TOPLEVEL_HPP

#include "types.hpp"

#include <wayland-server-core.h>

class ForeignToplevelHandle {
  public:
	struct listener_container {
		ForeignToplevelHandle* parent;
		wl_listener request_maximize;
		wl_listener request_minimize;
		wl_listener request_activate;
		wl_listener request_fullscreen;
		wl_listener request_close;
		wl_listener set_rectangle;
	};

  private:
	listener_container listeners;

  public:
	View& view;
	struct wlr_foreign_toplevel_handle_v1* wlr_handle;

	ForeignToplevelHandle(View& view);
	~ForeignToplevelHandle() noexcept;

	void set_title(char* title);
	void set_app_id(char* app_id);
	void set_parent(ForeignToplevelHandle* parent);
	void set_maximized(bool maximized);
	void set_minimized(bool minimized);
	void set_activated(bool activated);
	void set_fullscreen(bool fullscreen);
	void output_enter(Output& output);
	void output_leave(Output& output);
};

#endif
