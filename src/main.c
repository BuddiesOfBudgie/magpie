#define _POSIX_C_SOURCE 200809L

#include "server.h"

#include <assert.h>
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <wlr/util/log.h>

#define WLR_USE_UNSTABLE
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xcursor_manager.h>

int main(int argc, char** argv) {
    char* startup_cmd = NULL;
    int c;
    while ((c = getopt(argc, argv, "s:h")) != -1) {
        switch (c) {
            case 's':
                startup_cmd = optarg;
                break;
            default:
                printf("Usage: %s [-s startup command]\n", argv[0]);
                return 0;
        }
    }
    if (optind < argc) {
        printf("Usage: %s [-s startup command]\n", argv[0]);
        return 0;
    }

    wlr_log_init(WLR_INFO, NULL);

    magpie_server_t server = *new_magpie_server();

    /* Add a Unix socket to the Wayland display. */
    const char* socket = wl_display_add_socket_auto(server.wl_display);
    assert(socket);

    /* Start the backend. This will enumerate outputs and inputs, become the DRM master, etc */
    if (!wlr_backend_start(server.backend)) {
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.wl_display);
        return 1;
    }

    printf("Running compositor on wayland display '%s'\n", socket);
    setenv("WAYLAND_DISPLAY", socket, true);

    if (startup_cmd) {
        if (fork() == 0) {
            execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (void*) NULL);
        }
    }

    /* Run the Wayland event loop. This does not return until you exit the
     * compositor. Starting the backend rigged up all of the necessary event
     * loop configuration to listen to libinput events, DRM events, generate
     * frame events at the refresh rate, and so on. */
    wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s", socket);
    wl_display_run(server.wl_display);
    wl_display_destroy_clients(server.wl_display);
    wl_display_destroy(server.wl_display);

    return 0;
}