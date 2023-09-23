#ifndef MAGPIE_VIEW_HPP
#define MAGPIE_VIEW_HPP

#include "input/cursor.hpp"
#include "types.hpp"

#include <optional>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>
#include <wlr/xwayland.h>
#include "wlr-wrap-end.hpp"

class View {
  public:
	bool maximized;
	wlr_box current;
	wlr_box pending;
	wlr_box previous;
	wlr_surface* surface;
	wlr_scene_node* scene_node;
	ForeignToplevelHandle* toplevel_handle;

	virtual ~View() noexcept {};

	virtual Server& get_server() = 0;
	virtual const wlr_box get_geometry() = 0;

	virtual void map() = 0;
	virtual void unmap() = 0;

	void begin_interactive(const CursorMode mode, const uint32_t edges);
	void set_size(const int new_width, const int new_height);
	void set_activated(const bool activated);
	void set_maximized(const bool maximized);

  private:
	const std::optional<const Output*> find_output_for_maximize();

  protected:
	virtual void impl_set_size(const int new_width, const int new_height) = 0;
	virtual void impl_set_activated(const bool activated) = 0;
	virtual void impl_set_maximized(const bool maximized) = 0;
};

class XdgView : public View {
  public:
	struct Listeners {
		XdgView* parent;
		wl_listener map;
		wl_listener unmap;
		wl_listener destroy;
		wl_listener commit;
		wl_listener request_move;
		wl_listener request_resize;
		wl_listener request_maximize;
		wl_listener request_unmaximize;
		wl_listener request_fullscreen;
		wl_listener set_title;
		wl_listener set_app_id;
		wl_listener set_parent;
	};

  private:
	Listeners listeners;

  public:
	Server& server;
	wlr_xdg_toplevel* xdg_toplevel;

	XdgView(Server& server, wlr_xdg_toplevel* toplevel) noexcept;
	~XdgView() noexcept;

	inline Server& get_server();
	const wlr_box get_geometry();
	void map();
	void unmap();

  protected:
	void impl_set_size(int new_width, int new_height);
	void impl_set_activated(bool activated);
	void impl_set_maximized(bool maximized);
};

class XWaylandView : public View {
  public:
	struct Listeners {
		XWaylandView* parent;
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
	};

  private:
	Listeners listeners;

  public:
	Server& server;
	wlr_xwayland_surface* xwayland_surface;

	XWaylandView(Server& server, wlr_xwayland_surface* surface) noexcept;
	~XWaylandView() noexcept;

	inline Server& get_server();
	const wlr_box get_geometry();
	void map();
	void unmap();

  protected:
	void impl_set_size(int new_width, int new_height);
	void impl_set_activated(bool activated);
	void impl_set_maximized(bool maximized);
};

#endif
