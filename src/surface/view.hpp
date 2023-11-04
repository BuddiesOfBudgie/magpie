#ifndef MAGPIE_VIEW_HPP
#define MAGPIE_VIEW_HPP

#include "foreign_toplevel.hpp"
#include "input/cursor.hpp"
#include "surface.hpp"
#include "types.hpp"

#include <optional>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>
#include <wlr/xwayland.h>
#include "wlr-wrap-end.hpp"

struct View : public Surface {
	bool is_maximized;
	bool is_minimized;
	wlr_box current;
	wlr_box pending;
	wlr_box previous;
	std::optional<ForeignToplevelHandle> toplevel_handle = {};

	virtual ~View() noexcept {};

	virtual const wlr_box get_geometry() const = 0;
	virtual void map() = 0;
	virtual void unmap() = 0;

	constexpr bool is_view() const override {
		return true;
	}
	void begin_interactive(const CursorMode mode, const uint32_t edges);
	void set_position(const int new_x, const int new_y);
	void set_size(const int new_width, const int new_height);
	void set_activated(const bool activated);
	void set_maximized(const bool maximized);
	void set_minimized(const bool minimized);

  private:
	const std::optional<const Output*> find_output_for_maximize();

  protected:
	virtual void impl_set_position(const int new_x, const int new_y) = 0;
	virtual void impl_set_size(const int new_width, const int new_height) = 0;
	virtual void impl_set_activated(const bool activated) = 0;
	virtual void impl_set_fullscreen(const bool fullscreen) = 0;
	virtual void impl_set_maximized(const bool maximized) = 0;
	virtual void impl_set_minimized(const bool minimized) = 0;
};

class XdgView : public View {
  public:
	struct Listeners {
		std::reference_wrapper<XdgView> parent;
		wl_listener map;
		wl_listener unmap;
		wl_listener destroy;
		wl_listener commit;
		wl_listener request_move;
		wl_listener request_resize;
		wl_listener request_maximize;
		wl_listener request_minimize;
		wl_listener request_fullscreen;
		wl_listener set_title;
		wl_listener set_app_id;
		wl_listener set_parent;
		Listeners(XdgView& parent) noexcept : parent(parent) {}
	};

  private:
	Listeners listeners;
	bool pending_map = true;

  public:
	Server& server;
	wlr_xdg_toplevel& xdg_toplevel;

	XdgView(Server& server, wlr_xdg_toplevel& toplevel) noexcept;
	~XdgView() noexcept;

	constexpr wlr_surface* get_wlr_surface() const override;
	constexpr Server& get_server() const override;
	const wlr_box get_geometry() const override;
	void map() override;
	void unmap() override;

  protected:
	void impl_set_position(int new_x, int new_y) override;
	void impl_set_size(int new_width, int new_height) override;
	void impl_set_activated(bool activated) override;
	void impl_set_fullscreen(bool fullscreen) override;
	void impl_set_maximized(bool maximized) override;
	void impl_set_minimized(bool minimized) override;
};

class XWaylandView : public View {
  public:
	struct Listeners {
		std::reference_wrapper<XWaylandView> parent;
		wl_listener map;
		wl_listener unmap;
		wl_listener destroy;
		wl_listener commit;
		wl_listener request_configure;
		wl_listener request_move;
		wl_listener request_resize;
		wl_listener set_geometry;
		wl_listener set_title;
		wl_listener set_class;
		wl_listener set_parent;
		Listeners(XWaylandView& parent) noexcept : parent(parent) {}
	};

  private:
	Listeners listeners;

  public:
	Server& server;
	wlr_xwayland_surface& xwayland_surface;

	XWaylandView(Server& server, wlr_xwayland_surface& surface) noexcept;
	~XWaylandView() noexcept;

	constexpr wlr_surface* get_wlr_surface() const override;
	constexpr Server& get_server() const override;
	const wlr_box get_geometry() const override;
	void map() override;
	void unmap() override;

  protected:
	void impl_set_position(int new_x, int new_y) override;
	void impl_set_size(int new_width, int new_height) override;
	void impl_set_activated(bool activated) override;
	void impl_set_fullscreen(bool fullscreen) override;
	void impl_set_maximized(bool maximized) override;
	void impl_set_minimized(bool minimized) override;
};

#endif
