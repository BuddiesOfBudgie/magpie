#ifndef MAGPIE_POPUP_HPP
#define MAGPIE_POPUP_HPP

#include "surface.hpp"
#include "types.hpp"

#include <functional>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_xdg_shell.h>
#include "wlr-wrap-end.hpp"

class Popup final : public Surface {
  public:
	struct Listeners {
		std::reference_wrapper<Popup> parent;
		wl_listener map = {};
		wl_listener destroy = {};
		wl_listener new_popup = {};
		explicit Listeners(Popup& parent) noexcept : parent(parent) {}
	};

  private:
	Listeners listeners;

  public:
	Server& server;
	Surface& parent;
	wlr_xdg_popup& wlr;

	Popup(Surface& parent, wlr_xdg_popup& wlr) noexcept;
	~Popup() noexcept override;

	[[nodiscard]] constexpr wlr_surface* get_wlr_surface() const override;
	[[nodiscard]] constexpr Server& get_server() const override;
	[[nodiscard]] constexpr bool is_view() const override;
};

#endif
