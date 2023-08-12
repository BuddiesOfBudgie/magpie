#ifndef MAGPIE_INPUT_H
#define MAGPIE_INPUT_H

#include "types.h"

struct magpie_keyboard {
    magpie_server_t* server;

    struct wl_list link;
    struct wlr_keyboard* wlr_keyboard;

    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
};

typedef enum { MAGPIE_CURSOR_PASSTHROUGH, MAGPIE_CURSOR_MOVE, MAGPIE_CURSOR_RESIZE } magpie_cursor_mode_t;

void new_input_notify(struct wl_listener* listener, void* data);
void request_cursor_notify(struct wl_listener* listener, void* data);
void cursor_motion_notify(struct wl_listener* listener, void* data);
void cursor_motion_absolute_notify(struct wl_listener* listener, void* data);
void cursor_button_notify(struct wl_listener* listener, void* data);
void cursor_axis_notify(struct wl_listener* listener, void* data);
void cursor_frame_notify(struct wl_listener* listener, void* data);
void seat_request_set_selection(struct wl_listener* listener, void* data);
void reset_cursor_mode(magpie_server_t* server);

#endif
