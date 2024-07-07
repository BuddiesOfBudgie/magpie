#include "subsurface.hpp"

#include "types.hpp"

#include <utility>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/util/log.h>
#include "wlr-wrap-end.hpp"

static void subsurface_destroy_notify(wl_listener* listener, [[maybe_unused]] void* data) {
	wlr_log(WLR_DEBUG, "wlr_subsurface.events.destroy(listener=%p, data=%p)", (void*) listener, data);

	Subsurface& subsurface = magpie_container_of(listener, subsurface, destroy);

	subsurface.parent.subsurfaces.erase(std::dynamic_pointer_cast<Subsurface>(subsurface.shared_from_this()));
}

Subsurface::Subsurface(Surface& parent, wlr_subsurface& subsurface) noexcept
	: listeners(*this), parent(parent), wlr(subsurface) {
	listeners.destroy.notify = subsurface_destroy_notify;
	wl_signal_add(&subsurface.events.destroy, &listeners.destroy);
}

Subsurface::~Subsurface() noexcept {
	wl_list_remove(&listeners.map.link);
	wl_list_remove(&listeners.destroy.link);
}

wlr_surface* Subsurface::get_wlr_surface() const {
	return wlr.surface;
}

Server& Subsurface::get_server() const {
	return parent.get_server();
}

bool Subsurface::is_view() const {
	return false;
}
