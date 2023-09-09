#ifndef MAGPIE_SEAT_HPP
#define MAGPIE_SEAT_HPP

#include "types.hpp"

#include <vector>
#include <wayland-server-core.h>



class Seat {
  public:
	struct Listeners {
		Seat* parent;
		wl_listener new_input;
		wl_listener request_cursor;
		wl_listener request_set_selection;
	};

  private:
	Listeners listeners;

  public:
	Server& server;
	struct wlr_seat* wlr_seat;
	Cursor* cursor;
	std::vector<Keyboard*> keyboards;

	Seat(Server& server);

	void new_input_device(struct wlr_input_device* device);
};

#endif
