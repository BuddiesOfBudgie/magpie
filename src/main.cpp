#include "server.hpp"

#include <csignal>
#include <cstdio>
#include <optional>
#include <string>
#include <thread>
#include <unistd.h>
#include <utility>

#include "wlr-wrap-start.hpp"
#include <wlr/backend.h>
#include <wlr/util/log.h>
#include "wlr-wrap-end.hpp"

static pthread_t main_thread;

static void kiosk_run(const int32_t argc, char** argv) {
	if (argc != 1)
		return;
	system(argv[0]);
	pthread_kill(main_thread, SIGINT);
}

int32_t main(const int32_t argc, char** argv) {
	std::optional<std::string> kiosk_cmd;
	std::vector<std::string> startup_cmds;

	int32_t c;
	while ((c = getopt(argc, argv, "s:k:h")) != -1) {
		switch (c) {
			case 's':
				if (kiosk_cmd.has_value()) {
					std::printf("-s and -k options are mutually exclusive\n");
					return 1;
				}
				startup_cmds.emplace_back(optarg);
				break;
			case 'k':
				if (!startup_cmds.empty()) {
					std::printf("-s and -k options are mutually exclusive\n");
					return 1;
				}
				kiosk_cmd = optarg;
				break;
			default:
				std::printf("Usage: %s [-s <startup command>] [-k <kiosk command>]\n", argv[0]);
				return 0;
		}
	}

	if (optind < argc) {
		std::printf("Usage: %s [-s <startup command>] [-k <kiosk command>]\n", argv[0]);
		return 0;
	}

	wlr_log_init(WLR_INFO, nullptr);

	const auto server = Server();

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

	setenv("WAYLAND_DISPLAY", socket, true);

	if (kiosk_cmd.has_value()) {
		wlr_log(WLR_INFO, "Running in kiosk mode with command '%s'.", kiosk_cmd->c_str());
		main_thread = pthread_self();
		char* kiosk_argv[1] = {strdup(kiosk_cmd.value().c_str())};
		auto kiosk_thread = std::thread(kiosk_run, 1, kiosk_argv);
		kiosk_thread.detach();
	} else
		for (const auto& cmd : std::as_const(startup_cmds)) {
			if (fork() == 0) {
				execl("/bin/sh", "/bin/sh", "-c", cmd.c_str(), nullptr);
			}
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
