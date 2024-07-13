#include "foreign_toplevel.hpp"
#include "output.hpp"

#include "server.hpp"
#include "surface/view.hpp"

#include "wlr-wrap-start.hpp"
#include <wlr/util/log.h>
#include "wlr-wrap-end.hpp"

static void foreign_toplevel_handle_request_maximize_notify(wl_listener* listener, void* data) {
	wlr_log(WLR_DEBUG, "wlr_foreign_toplevel_handle_v1.events.request_maximize(listener=%p, data=%p)", (void*) listener, data);

	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_foreign_toplevel_handle_v1.events.request_maximize");
		return;
	}

	const ForeignToplevelHandle& handle = magpie_container_of(listener, handle, request_maximize);
	const auto& event = *static_cast<wlr_foreign_toplevel_handle_v1_maximized_event*>(data);

	const auto placement = event.maximized ? VIEW_PLACEMENT_MAXIMIZED : VIEW_PLACEMENT_STACKING;
	handle.view.set_placement(placement);
}

static void foreign_toplevel_handle_request_fullscreen_notify(wl_listener* listener, void* data) {
	wlr_log(
		WLR_DEBUG, "wlr_foreign_toplevel_handle_v1.events.request_fullscreen(listener=%p, data=%p)", (void*) listener, data);

	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_foreign_toplevel_handle_v1.events.request_fullscreen");
		return;
	}

	const ForeignToplevelHandle& handle = magpie_container_of(listener, handle, request_fullscreen);
	const auto& event = *static_cast<wlr_foreign_toplevel_handle_v1_maximized_event*>(data);

	const auto placement = event.maximized ? VIEW_PLACEMENT_FULLSCREEN : VIEW_PLACEMENT_STACKING;
	handle.view.set_placement(placement);
}

static void foreign_toplevel_handle_request_minimize_notify(wl_listener* listener, void* data) {
	wlr_log(WLR_DEBUG, "wlr_foreign_toplevel_handle_v1.events.request_minimize(listener=%p, data=%p)", (void*) listener, data);

	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_foreign_toplevel_handle_v1.events.request_minimize");
		return;
	}

	const ForeignToplevelHandle& handle = magpie_container_of(listener, handle, request_minimize);
	const auto& event = *static_cast<wlr_foreign_toplevel_handle_v1_minimized_event*>(data);

	handle.view.set_minimized(event.minimized);
}

static void foreign_toplevel_handle_request_activate_notify(wl_listener* listener, [[maybe_unused]] void* data) {
	wlr_log(WLR_DEBUG, "wlr_foreign_toplevel_handle_v1.events.request_activate(listener=%p, data=%p)", (void*) listener, data);

	const ForeignToplevelHandle& handle = magpie_container_of(listener, handle, request_activate);

	handle.view.set_minimized(false);
	handle.view.get_server().focus_view(std::dynamic_pointer_cast<View>(handle.view.shared_from_this()));
}

static void foreign_toplevel_handle_request_close_notify(wl_listener* listener, [[maybe_unused]] void* data) {
	wlr_log(WLR_DEBUG, "wlr_foreign_toplevel_handle_v1.events.request_close(listener=%p, data=%p)", (void*) listener, data);

	const ForeignToplevelHandle& handle = magpie_container_of(listener, handle, request_close);

	handle.view.close();
}

static void foreign_toplevel_handle_set_rectangle_notify(wl_listener* listener, void* data) {
	wlr_log(WLR_DEBUG, "wlr_foreign_toplevel_handle_v1.events.set_rectangle(listener=%p, data=%p)", (void*) listener, data);

	if (data == nullptr) {
		wlr_log(WLR_ERROR, "No data passed to wlr_foreign_toplevel_handle_v1.events.set_rectangle");
		return;
	}

	const ForeignToplevelHandle& handle = magpie_container_of(listener, handle, set_rectangle);
	const auto& event = *static_cast<wlr_foreign_toplevel_handle_v1_set_rectangle_event*>(data);

	handle.view.set_position(event.x, event.y);
	handle.view.set_size(event.width, event.height);
}

ForeignToplevelHandle::ForeignToplevelHandle(View& view) noexcept
	: listeners(*this), view(view), wlr(*wlr_foreign_toplevel_handle_v1_create(view.get_server().foreign_toplevel_manager)) {
	wlr.data = this;

	listeners.request_maximize.notify = foreign_toplevel_handle_request_maximize_notify;
	wl_signal_add(&wlr.events.request_maximize, &listeners.request_maximize);
	listeners.request_minimize.notify = foreign_toplevel_handle_request_minimize_notify;
	wl_signal_add(&wlr.events.request_minimize, &listeners.request_minimize);
	listeners.request_activate.notify = foreign_toplevel_handle_request_activate_notify;
	wl_signal_add(&wlr.events.request_activate, &listeners.request_activate);
	listeners.request_fullscreen.notify = foreign_toplevel_handle_request_fullscreen_notify;
	wl_signal_add(&wlr.events.request_fullscreen, &listeners.request_fullscreen);
	listeners.request_close.notify = foreign_toplevel_handle_request_close_notify;
	wl_signal_add(&wlr.events.request_close, &listeners.request_close);
	listeners.set_rectangle.notify = foreign_toplevel_handle_set_rectangle_notify;
	wl_signal_add(&wlr.events.set_rectangle, &listeners.set_rectangle);
}

ForeignToplevelHandle::~ForeignToplevelHandle() noexcept {
	wlr_foreign_toplevel_handle_v1_destroy(&wlr);
	wl_list_remove(&listeners.request_maximize.link);
	wl_list_remove(&listeners.request_minimize.link);
	wl_list_remove(&listeners.request_activate.link);
	wl_list_remove(&listeners.request_fullscreen.link);
	wl_list_remove(&listeners.request_close.link);
	wl_list_remove(&listeners.set_rectangle.link);
}

void ForeignToplevelHandle::set_title(const char* title) const {
	if (title != nullptr) {
		wlr_foreign_toplevel_handle_v1_set_title(&wlr, title);
	}
}

void ForeignToplevelHandle::set_app_id(const char* app_id) const {
	if (app_id != nullptr) {
		wlr_foreign_toplevel_handle_v1_set_app_id(&wlr, app_id);
	}
}

void ForeignToplevelHandle::set_parent(const std::optional<ForeignToplevelHandle>& parent) const {
	wlr_foreign_toplevel_handle_v1_set_parent(&wlr, parent.has_value() ? &parent.value().wlr : nullptr);
}

void ForeignToplevelHandle::set_placement(const ViewPlacement placement) const {
	set_maximized(placement == VIEW_PLACEMENT_MAXIMIZED);
	set_fullscreen(placement == VIEW_PLACEMENT_FULLSCREEN);
}

void ForeignToplevelHandle::set_maximized(const bool maximized) const {
	wlr_foreign_toplevel_handle_v1_set_maximized(&wlr, maximized);
}

void ForeignToplevelHandle::set_minimized(const bool minimized) const {
	wlr_foreign_toplevel_handle_v1_set_minimized(&wlr, minimized);
}

void ForeignToplevelHandle::set_activated(const bool activated) const {
	wlr_foreign_toplevel_handle_v1_set_activated(&wlr, activated);
}

void ForeignToplevelHandle::set_fullscreen(const bool fullscreen) const {
	wlr_foreign_toplevel_handle_v1_set_fullscreen(&wlr, fullscreen);
}

void ForeignToplevelHandle::output_enter(const Output& output) const {
	wlr_foreign_toplevel_handle_v1_output_enter(&wlr, &output.wlr);
}

void ForeignToplevelHandle::output_leave(const Output& output) const {
	wlr_foreign_toplevel_handle_v1_output_leave(&wlr, &output.wlr);
}
