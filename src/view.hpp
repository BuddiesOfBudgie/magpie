#ifndef MAGPIE_VIEW_HPP
#define MAGPIE_VIEW_HPP

#include "input/cursor.hpp"
#include "types.hpp"

#include "wlr-wrap-start.hpp"
#include <wlr/xwayland.h>
#include <wlr/util/box.h>
#include "wlr-wrap-end.hpp"

class View {
  public:
	struct wlr_box current;
	struct wlr_box pending;
	struct wlr_box previous;
	struct wlr_surface* surface;
	struct wlr_scene_node* scene_node;
	ForeignToplevelHandle* toplevel_handle;

	virtual ~View() noexcept {};

	virtual Server& get_server() = 0;
	virtual struct wlr_box get_geometry() = 0;
	virtual void set_size(int new_width, int new_height) = 0;
	virtual void begin_interactive(CursorMode mode, uint32_t edges) = 0;

	virtual void set_activated(bool activated) = 0;
	virtual void set_maximized(bool maximized) = 0;
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
	struct wlr_xdg_toplevel* xdg_toplevel;

	XdgView(Server& server, struct wlr_xdg_toplevel* toplevel);
	~XdgView() noexcept;

	inline Server& get_server();
	struct wlr_box get_geometry();
	void set_size(int new_width, int new_height);
	void begin_interactive(CursorMode mode, uint32_t edges);

	void set_activated(bool activated);
	void set_maximized(bool maximized);
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
	struct wlr_xwayland_surface* xwayland_surface;

	XWaylandView(Server& server, struct wlr_xwayland_surface* surface);
	~XWaylandView() noexcept;

	inline Server& get_server();
	struct wlr_box get_geometry();
	void set_size(int new_width, int new_height);
	void begin_interactive(CursorMode mode, uint32_t edges);

	void set_activated(bool activated);
	void set_maximized(bool maximized);
};

#endif
