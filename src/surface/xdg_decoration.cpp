#include "xdg_decoration.hpp"

#include "view.hpp"

#include "wlr-wrap-start.hpp"
#include <wlr/util/log.h>
#include "wlr-wrap-end.hpp"

static void xdg_decoration_destroy_notify(wl_listener* listener, [[maybe_unused]] void* data) {
	wlr_log(WLR_DEBUG, "wlr_xdg_toplevel_decoration_v1.events.destroy(listener=%p, data=%p)", (void*) listener, data);

	XdgDecoration& deco = magpie_container_of(listener, deco, destroy);

	deco.view.set_decoration(nullptr);
}

XdgDecoration::XdgDecoration(XdgView& view, wlr_xdg_toplevel_decoration_v1& deco) noexcept
	: listeners(*this), view(view), wlr(deco) {
	listeners.destroy.notify = xdg_decoration_destroy_notify;
	wl_signal_add(&deco.events.destroy, &listeners.destroy);
}

XdgDecoration::~XdgDecoration() noexcept {
	wl_list_remove(&listeners.destroy.link);
}
