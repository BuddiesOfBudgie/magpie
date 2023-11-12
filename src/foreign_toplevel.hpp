#ifndef FOREIGN_TOPLEVEL_HPP
#define FOREIGN_TOPLEVEL_HPP

#include "types.hpp"

#include <functional>
#include <optional>
#include <string>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include "wlr-wrap-end.hpp"

class ForeignToplevelHandle {
  public:
	struct Listeners {
		std::reference_wrapper<ForeignToplevelHandle> parent;
		wl_listener request_maximize = {};
		wl_listener request_minimize = {};
		wl_listener request_activate = {};
		wl_listener request_fullscreen = {};
		wl_listener request_close = {};
		wl_listener set_rectangle = {};
		explicit Listeners(ForeignToplevelHandle& parent) noexcept : parent(parent) {}
	};

  private:
	Listeners listeners;

  public:
	View& view;
	wlr_foreign_toplevel_handle_v1& handle;

	explicit ForeignToplevelHandle(View& view) noexcept;
	~ForeignToplevelHandle() noexcept;

	void set_title(const std::string& title) const;
	void set_app_id(const std::string& app_id) const;
	void set_parent(std::optional<std::reference_wrapper<const ForeignToplevelHandle>> parent) const;
	void set_placement(ViewPlacement placement) const;
	void set_maximized(bool maximized) const;
	void set_fullscreen(bool fullscreen) const;
	void set_minimized(bool minimized) const;
	void set_activated(bool activated) const;
	void output_enter(const Output& output) const;
	void output_leave(const Output& output) const;
};

#endif
