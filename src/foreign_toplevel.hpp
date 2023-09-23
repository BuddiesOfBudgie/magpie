#ifndef FOREIGN_TOPLEVEL_HPP
#define FOREIGN_TOPLEVEL_HPP

#include "types.hpp"

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include "wlr-wrap-end.hpp"

class ForeignToplevelHandle {
  public:
	struct Listeners {
		ForeignToplevelHandle* parent;
		wl_listener request_maximize;
		wl_listener request_minimize;
		wl_listener request_activate;
		wl_listener request_fullscreen;
		wl_listener request_close;
		wl_listener set_rectangle;
	};

  private:
	Listeners listeners;

  public:
	View& view;
	wlr_foreign_toplevel_handle_v1* handle;

	ForeignToplevelHandle(View& view) noexcept;
	~ForeignToplevelHandle() noexcept;

	void set_title(const char* title);
	void set_app_id(const char* app_id);
	void set_parent(const ForeignToplevelHandle* parent);
	void set_maximized(bool maximized);
	void set_minimized(bool minimized);
	void set_activated(bool activated);
	void set_fullscreen(bool fullscreen);
	void output_enter(const Output& output);
	void output_leave(const Output& output);
};

#endif
