#include "foreign_toplevel.hpp"

#include "output.hpp"
#include "server.hpp"
#include "view.hpp"

static void foreign_toplevel_handle_request_maximize_notify(wl_listener* listener, void* data) {
	const ForeignToplevelHandle& handle = *magpie_container_of(listener, handle, request_activate);
	auto& event = *static_cast<wlr_foreign_toplevel_handle_v1_maximized_event*>(data);

	handle.view.set_maximized(event.maximized);
}

static void foreign_toplevel_handle_request_minimize_notify(wl_listener* listener, void* data) {
	(void) listener;
	(void) data;
}

static void foreign_toplevel_handle_request_activate_notify(wl_listener* listener, void* data) {
	const ForeignToplevelHandle& handle = *magpie_container_of(listener, handle, request_activate);
	(void) data;

	handle.view.set_activated(true);
}

static void foreign_toplevel_handle_request_fullscreen_notify(wl_listener* listener, void* data) {
	(void) listener;
	(void) data;
}

static void foreign_toplevel_handle_request_close_notify(wl_listener* listener, void* data) {
	(void) listener;
	(void) data;
}

static void foreign_toplevel_handle_set_rectangle_notify(wl_listener* listener, void* data) {
	(void) listener;
	(void) data;
}

ForeignToplevelHandle::ForeignToplevelHandle(View& view) noexcept : view(view) {
	listeners.parent = this;

	handle = wlr_foreign_toplevel_handle_v1_create(view.get_server().foreign_toplevel_manager);
	handle->data = this;

	listeners.request_maximize.notify = foreign_toplevel_handle_request_maximize_notify;
	wl_signal_add(&handle->events.request_maximize, &listeners.request_maximize);
	listeners.request_minimize.notify = foreign_toplevel_handle_request_minimize_notify;
	wl_signal_add(&handle->events.request_minimize, &listeners.request_minimize);
	listeners.request_activate.notify = foreign_toplevel_handle_request_activate_notify;
	wl_signal_add(&handle->events.request_activate, &listeners.request_activate);
	listeners.request_fullscreen.notify = foreign_toplevel_handle_request_fullscreen_notify;
	wl_signal_add(&handle->events.request_fullscreen, &listeners.request_fullscreen);
	listeners.request_close.notify = foreign_toplevel_handle_request_close_notify;
	wl_signal_add(&handle->events.request_close, &listeners.request_close);
	listeners.set_rectangle.notify = foreign_toplevel_handle_set_rectangle_notify;
	wl_signal_add(&handle->events.set_rectangle, &listeners.set_rectangle);
}

ForeignToplevelHandle::~ForeignToplevelHandle() noexcept {
	wlr_foreign_toplevel_handle_v1_destroy(handle);
	wl_list_remove(&listeners.request_maximize.link);
	wl_list_remove(&listeners.request_minimize.link);
	wl_list_remove(&listeners.request_activate.link);
	wl_list_remove(&listeners.request_fullscreen.link);
	wl_list_remove(&listeners.request_close.link);
	wl_list_remove(&listeners.set_rectangle.link);
}

void ForeignToplevelHandle::set_title(const char* title) {
	if (title != nullptr) {
		wlr_foreign_toplevel_handle_v1_set_title(handle, title);
	}
}

void ForeignToplevelHandle::set_app_id(const char* app_id) {
	if (app_id != nullptr) {
		wlr_foreign_toplevel_handle_v1_set_app_id(handle, app_id);
	}
}

void ForeignToplevelHandle::set_parent(const ForeignToplevelHandle* parent) {
	wlr_foreign_toplevel_handle_v1_set_parent(handle, (parent == nullptr) ? nullptr : parent->handle);
}

void ForeignToplevelHandle::set_maximized(const bool maximized) {
	wlr_foreign_toplevel_handle_v1_set_maximized(handle, maximized);
}

void ForeignToplevelHandle::set_minimized(const bool minimized) {
	wlr_foreign_toplevel_handle_v1_set_minimized(handle, minimized);
}

void ForeignToplevelHandle::set_activated(const bool activated) {
	wlr_foreign_toplevel_handle_v1_set_activated(handle, activated);
}

void ForeignToplevelHandle::set_fullscreen(const bool fullscreen) {
	wlr_foreign_toplevel_handle_v1_set_fullscreen(handle, fullscreen);
}

void ForeignToplevelHandle::output_enter(const Output& output) {
	wlr_foreign_toplevel_handle_v1_output_enter(handle, output.output);
}

void ForeignToplevelHandle::output_leave(const Output& output) {
	wlr_foreign_toplevel_handle_v1_output_leave(handle, output.output);
}
