#include "server.hpp"

#include <argparse/argparse.hpp>
#include <cstdio>
#include <future>
#include <optional>
#include <string>
#include <thread>
#include <unistd.h>
#include <utility>

#include "wlr-wrap-start.hpp"
#include <wlr/backend.h>
#include <wlr/util/log.h>
#include "wlr-wrap-end.hpp"

int32_t run_compositor(const std::vector<std::string>& startup_cmds, std::promise<const char*> socket_promise = {}) {
	const auto server = Server();

	/* Add a Unix socket to the Wayland display. */
	const char* socket = wl_display_add_socket_auto(server.display);
	if (socket == nullptr) {
		std::printf("Unix socket for display failed to initialize\n");
		return 1;
	}

	socket_promise.set_value(socket);

	/* Start the backend. This will enumerate outputs and inputs, become the DRM master, etc */
	if (!wlr_backend_start(server.backend)) {
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.display);
		return 1;
	}

	setenv("WAYLAND_DISPLAY", socket, 1);
	wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s", socket);

	for (const auto& cmd : std::as_const(startup_cmds)) {
		if (fork() == 0) {
			execl("/bin/sh", "/bin/sh", "-c", cmd.c_str(), nullptr);
		}
	}

	wl_display_run(server.display);
	wl_display_destroy_clients(server.display);
	wl_display_destroy(server.display);

	return 0;
}

int32_t main(const int32_t argc, char** argv) {
	auto argparser = argparse::ArgumentParser(argv[0], PROJECT_VERSION);

	auto& subprocess_group = argparser.add_mutually_exclusive_group();
	subprocess_group.add_argument("-k", "--kiosk")
		.help("specify a single executable whose lifecycle will be adopted, such as a login manager")
		.nargs(1);
	subprocess_group.add_argument("-s", "--subprocess")
		.help("specify one or more executables which will be started as detached subprocesses")
		.nargs(argparse::nargs_pattern::at_least_one);

	try {
		argparser.parse_args(argc, argv);
	} catch (const std::exception& err) {
		std::cerr << err.what() << std::endl;
		std::cerr << argparser;
		return 1;
	}

	std::optional<std::string> kiosk_cmd;
	std::vector<std::string> startup_cmds;

	try {
		kiosk_cmd = argparser.present("--kiosk");
		startup_cmds = argparser.get<std::vector<std::string>>("--subprocess");
	} catch (const std::exception& err) {
		std::cerr << err.what() << std::endl;
		std::cerr << argparser;
		return 1;
	}

	wlr_log_init(WLR_INFO, nullptr);

	if (kiosk_cmd.has_value()) {
		wlr_log(WLR_INFO, "Running in kiosk mode with command '%s'.", kiosk_cmd->c_str());
		std::promise<const char*> socket_promise;
		std::future<const char*> socket_future = socket_promise.get_future();
		auto display_thread = std::thread(run_compositor, startup_cmds, std::move(socket_promise));
		display_thread.detach();
		setenv("WAYLAND_DISPLAY", socket_future.get(), 1);
		return system(kiosk_cmd.value().c_str());
	}

	return run_compositor(startup_cmds);
}
