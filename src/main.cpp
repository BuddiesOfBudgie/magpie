#define _POSIX_C_SOURCE 200809L

#include "server.hpp"

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
		std::printf("Usage: %s [-s startup command]\n", argv[0]);
		return 0;
	}

	wlr_log_init(WLR_INFO, NULL);

	Server server = Server();

	/* Add a Unix socket to the Wayland display. */
	const char* socket = wl_display_add_socket_auto(server.display);
	if (socket == nullptr) {
		std::printf("Unix socket for display failed to initialize\n");
		return 1;
	}

	/* Start the backend. This will enumerate outputs and inputs, become the DRM master, etc */
	if (!wlr_backend_start(server.backend)) {
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.display);
		return 1;
	}

	std::printf("Running compositor on wayland display '%s'\n", socket);
	setenv("WAYLAND_DISPLAY", socket, true);

	if (startup_cmd && fork() == 0) {
		execl("/bin/sh", "/bin/sh", "-c", startup_cmd, nullptr);
	}

	/* Run the Wayland event loop. This does not return until you exit the
	 * compositor. Starting the backend rigged up all of the necessary event
	 * loop configuration to listen to libinput events, DRM events, generate
	 * frame events at the refresh rate, and so on. */
	wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s", socket);
	wl_display_run(server.display);
	wl_display_destroy_clients(server.display);
	wl_display_destroy(server.display);

	return 0;
}