#ifndef MAGPIE_POPUP_HPP
#define MAGPIE_POPUP_HPP

#include "types.hpp"

#include <functional>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "wlr-wrap-end.hpp"

class Popup {
  public:
	struct Listeners {
		std::reference_wrapper<Popup> parent;
		wl_listener map;
		wl_listener unmap;
		wl_listener destroy;
		wl_listener commit;
		wl_listener new_popup;
		Listeners(Popup& parent) noexcept : parent(parent) {}
	};

  private:
	Listeners listeners;

  public:
	Server& server;
	Surface& parent;

	wlr_xdg_popup* xdg_popup;
	wlr_scene_node* scene_node;

	Popup(Surface& parent, wlr_xdg_popup* xdg_popup) noexcept;
	~Popup() noexcept;
};

#endif