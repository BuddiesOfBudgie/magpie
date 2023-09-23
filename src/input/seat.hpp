#ifndef MAGPIE_SEAT_HPP
#define MAGPIE_SEAT_HPP

#include "types.hpp"

#include <vector>

#include "wlr-wrap-start.hpp"
#include <wlr/types/wlr_seat.h>
#include "wlr-wrap-end.hpp"

class Seat {
  public:
	struct Listeners {
		std::reference_wrapper<Seat> parent;
		wl_listener new_input;
		wl_listener request_cursor;
		wl_listener request_set_selection;
		wl_listener destroy;
		Listeners(Seat& parent) noexcept : parent(std::ref(parent)) {}
	};

  private:
	Listeners listeners;

  public:
	Server& server;
	wlr_seat* seat;
	Cursor* cursor;
	std::vector<Keyboard*> keyboards;

	Seat(Server& server) noexcept;
	~Seat() noexcept;

	void new_input_device(wlr_input_device* device);
};

#endif
