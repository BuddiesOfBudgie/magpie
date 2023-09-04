#include "foreign_toplevel.hpp"

#include "output.hpp"
#include "server.hpp"
#include "view.hpp"

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include "wlr-wrap-end.hpp"

static void foreign_toplevel_handle_request_maximize_notify(wl_listener* listener, void* data) {
  	auto& event = *static_cast<struct wlr_foreign_toplevel_handle_v1_maximized_event*>(data);
  
	ForeignToplevelHandle::listener_container* container = wl_container_of(listener, container, request_activate);
	ForeignToplevelHandle& handle = *container->parent;
        
	handle.view.set_maximized(event.maximized);
}

static void foreign_toplevel_handle_request_minimize_notify(wl_listener* listener, void* data) {
	(void) listener;
	(void) data;
}

static void foreign_toplevel_handle_request_activate_notify(wl_listener* listener, void* data) {
	(void) data;

	ForeignToplevelHandle::listener_container* container = wl_container_of(listener, container, request_activate);
	ForeignToplevelHandle& handle = *container->parent;

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

ForeignToplevelHandle::ForeignToplevelHandle(View& view) : view(view) {
	listeners.parent = this;

	wlr_handle = wlr_foreign_toplevel_handle_v1_create(view.get_server().foreign_toplevel_manager);
	wlr_handle->data = this;

	listeners.request_maximize.notify = foreign_toplevel_handle_request_maximize_notify;
	wl_signal_add(&wlr_handle->events.request_maximize, &listeners.request_maximize);
	listeners.request_minimize.notify = foreign_toplevel_handle_request_minimize_notify;
	wl_signal_add(&wlr_handle->events.request_minimize, &listeners.request_minimize);
	listeners.request_activate.notify = foreign_toplevel_handle_request_activate_notify;
	wl_signal_add(&wlr_handle->events.request_activate, &listeners.request_activate);
	listeners.request_fullscreen.notify = foreign_toplevel_handle_request_fullscreen_notify;
	wl_signal_add(&wlr_handle->events.request_fullscreen, &listeners.request_fullscreen);
	listeners.request_close.notify = foreign_toplevel_handle_request_close_notify;
	wl_signal_add(&wlr_handle->events.request_close, &listeners.request_close);
	listeners.set_rectangle.notify = foreign_toplevel_handle_set_rectangle_notify;
	wl_signal_add(&wlr_handle->events.set_rectangle, &listeners.set_rectangle);
}

ForeignToplevelHandle::~ForeignToplevelHandle() noexcept {
	wlr_foreign_toplevel_handle_v1_destroy(wlr_handle);
	wl_list_remove(&listeners.request_maximize.link);
	wl_list_remove(&listeners.request_minimize.link);
	wl_list_remove(&listeners.request_activate.link);
	wl_list_remove(&listeners.request_fullscreen.link);
	wl_list_remove(&listeners.request_close.link);
	wl_list_remove(&listeners.set_rectangle.link);
}

void ForeignToplevelHandle::set_title(char* title) {
	if (title != nullptr) {
		wlr_foreign_toplevel_handle_v1_set_title(wlr_handle, title);
	}
}

void ForeignToplevelHandle::set_app_id(char* app_id) {
	if (app_id != nullptr) {
		wlr_foreign_toplevel_handle_v1_set_app_id(wlr_handle, app_id);
	}
}

void ForeignToplevelHandle::set_parent(ForeignToplevelHandle* parent) {
	wlr_foreign_toplevel_handle_v1_set_parent(wlr_handle, (parent == nullptr) ? nullptr : parent->wlr_handle);
}

void ForeignToplevelHandle::set_maximized(bool maximized) {
	wlr_foreign_toplevel_handle_v1_set_maximized(wlr_handle, maximized);
}

void ForeignToplevelHandle::set_minimized(bool minimized) {
	wlr_foreign_toplevel_handle_v1_set_minimized(wlr_handle, minimized);
}

void ForeignToplevelHandle::set_activated(bool activated) {
	wlr_foreign_toplevel_handle_v1_set_activated(wlr_handle, activated);
}

void ForeignToplevelHandle::set_fullscreen(bool fullscreen) {
	wlr_foreign_toplevel_handle_v1_set_fullscreen(wlr_handle, fullscreen);
}

void ForeignToplevelHandle::output_enter(Output& output) {
	wlr_foreign_toplevel_handle_v1_output_enter(wlr_handle, output.wlr_output);
}

void ForeignToplevelHandle::output_leave(Output& output) {
	wlr_foreign_toplevel_handle_v1_output_leave(wlr_handle, output.wlr_output);
}
