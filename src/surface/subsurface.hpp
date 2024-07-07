#ifndef MAGPIE_SUBSURFACE_HPP
#define MAGPIE_SUBSURFACE_HPP

#include "server.hpp"
#include "surface.hpp"
#include "types.hpp"

#include <functional>
#include <memory>
#include <set>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_subcompositor.h>
#include "wlr-wrap-end.hpp"

class Subsurface final : public Surface {
  public:
	struct Listeners {
		std::reference_wrapper<Subsurface> parent;
		wl_listener map = {};
		wl_listener destroy = {};
		explicit Listeners(Subsurface& parent) noexcept : parent(parent) {}
	};

  private:
	Listeners listeners;

  public:
	Surface& parent;
	wlr_subsurface& wlr;

	Subsurface(Surface& parent, wlr_subsurface& subsurface) noexcept;
	~Subsurface() noexcept override;

	[[nodiscard]] wlr_surface* get_wlr_surface() const override;
	[[nodiscard]] Server& get_server() const override;
	[[nodiscard]] bool is_view() const override;
};

#endif
