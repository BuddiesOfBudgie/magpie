#ifndef FOREIGN_TOPLEVEL_HPP
#define FOREIGN_TOPLEVEL_HPP

#include "types.hpp"

#include <functional>
#include <optional>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include "wlr-wrap-end.hpp"

class ForeignToplevelHandle {
  public:
	struct Listeners {
		std::reference_wrapper<ForeignToplevelHandle> parent;
		wl_listener request_maximize;
		wl_listener request_minimize;
		wl_listener request_activate;
		wl_listener request_fullscreen;
		wl_listener request_close;
		wl_listener set_rectangle;
		Listeners(ForeignToplevelHandle& parent) noexcept : parent(parent) {}
	};

  private:
	Listeners listeners;

  public:
	View& view;
	wlr_foreign_toplevel_handle_v1& handle;

	ForeignToplevelHandle(View& view) noexcept;
	~ForeignToplevelHandle() noexcept;

	void set_title(const char* title);
	void set_app_id(const char* app_id);
	void set_parent(std::optional<std::reference_wrapper<const ForeignToplevelHandle>> parent);
	void set_placement(const ViewPlacement placement);
	void set_maximized(const bool maximized);
	void set_fullscreen(const bool fullscreen);
	void set_minimized(const bool minimized);
	void set_activated(const bool activated);
	void output_enter(const Output& output);
	void output_leave(const Output& output);
};

#endif
