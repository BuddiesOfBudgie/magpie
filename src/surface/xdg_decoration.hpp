#ifndef MAGPIE_XDG_DECORATION_HPP
#define MAGPIE_XDG_DECORATION_HPP

#include "types.hpp"

#include <memory>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include "wlr-wrap-end.hpp"

class XdgDecoration : public std::enable_shared_from_this<XdgDecoration> {
  public:
	struct Listeners {
		std::reference_wrapper<XdgDecoration> parent;
		wl_listener destroy = {};
		explicit Listeners(XdgDecoration& parent) noexcept : parent(parent) {}
	};

  private:
	Listeners listeners;

  public:
	XdgView& view;
	wlr_xdg_toplevel_decoration_v1& wlr;

	XdgDecoration(XdgView& view, wlr_xdg_toplevel_decoration_v1& deco) noexcept;
	~XdgDecoration() noexcept;
};

#endif
