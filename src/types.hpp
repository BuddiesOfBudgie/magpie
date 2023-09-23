#ifndef MAGPIE_TYPES_HPP
#define MAGPIE_TYPES_HPP

class Server;
class XWayland;
class Output;

class Seat;
class Keyboard;
class Cursor;

class View;
class XdgView;
class XWaylandView;
class ForeignToplevelHandle;

class Layer;
class LayerSubsurface;

class Popup;

struct Surface;

#define magpie_container_of(ptr, sample, member)                                                                               \
	(__extension__({                                                                                                           \
		std::remove_reference<decltype(sample)>::type::Listeners* container = wl_container_of(ptr, container, member);         \
		container->parent;                                                                                                     \
	}))

#endif
