#include "xdg_decoration.hpp"

#include "ssd.hpp"
#include "surface/view.hpp"

#include "wlr-wrap-start.hpp"
#include <wlr/util/log.h>
#include "wlr-wrap-end.hpp"

static void xdg_decoration_destroy_notify(wl_listener* listener, [[maybe_unused]] void* data) {
	wlr_log(WLR_DEBUG, "wlr_xdg_toplevel_decoration_v1.events.destroy(listener=%p, data=%p)", (void*) listener, data);

	XdgDecoration& deco = magpie_container_of(listener, deco, destroy);

	deco.view.set_decoration(nullptr);
}

static void xdg_decoration_request_mode_notify(wl_listener* listener, [[maybe_unused]] void* data) {
	wlr_log(WLR_DEBUG, "wlr_xdg_toplevel_decoration_v1.events.request_mode(listener=%p, data=%p)", (void*) listener, data);

	XdgDecoration& deco = magpie_container_of(listener, deco, request_mode);

	if (deco.wlr.requested_mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE) {
		deco.view.ssd.emplace(deco.view);
	} else {
		deco.view.ssd.reset();
	}

	deco.view.update_surface_node_position();
}

XdgDecoration::XdgDecoration(XdgView& view, wlr_xdg_toplevel_decoration_v1& deco) noexcept
	: listeners(*this), view(view), wlr(deco) {
	listeners.destroy.notify = xdg_decoration_destroy_notify;
	wl_signal_add(&deco.events.destroy, &listeners.destroy);
	listeners.request_mode.notify = xdg_decoration_request_mode_notify;
	wl_signal_add(&deco.events.request_mode, &listeners.request_mode);
}

XdgDecoration::~XdgDecoration() noexcept {
	wl_list_remove(&listeners.destroy.link);
}
