#define _POSIX_C_SOURCE 200809L

#include "server.hpp"

#include <cstdlib>
#include <cstdio>
#include <getopt.h>
#include <unistd.h>

#include "wlr-wrap-start.hpp"
#include <wlr/backend.h>
#include <wlr/util/log.h>
#include "wlr-wrap-end.hpp"

int main(int argc, char** argv) {
	char* startup_cmd = NULL;
	int c;
	while ((c = getopt(argc, argv, "s:h")) != -1) {
		switch (c) {
			case 's':
				startup_cmd = optarg;
				break;
			default:
				std::printf("Usage: %s [-s startup command]\n", argv[0]);
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
