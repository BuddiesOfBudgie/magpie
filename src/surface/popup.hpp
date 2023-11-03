#ifndef MAGPIE_POPUP_HPP
#define MAGPIE_POPUP_HPP

#include "surface.hpp"
#include "types.hpp"

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_xdg_shell.h>
#include "wlr-wrap-end.hpp"

class Popup : public Surface {
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
	const Surface& parent;
	wlr_xdg_popup* xdg_popup;

	Popup(const Surface& parent, wlr_xdg_popup* xdg_popup) noexcept;
	~Popup() noexcept;

	constexpr Server& get_server() const override;
	constexpr bool is_view() const override;
};

#endif
