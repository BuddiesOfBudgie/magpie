#ifndef MAGPIE_TYPES_HPP
#define MAGPIE_TYPES_HPP

class Server;
class XWayland;
class Output;

class Seat;
class Keyboard;
class Cursor;
class PointerConstraint;

struct Surface;
struct View;
class XdgView;
class XWaylandView;
class Layer;
class LayerSubsurface;
class Popup;

class ForeignToplevelHandle;

#define magpie_container_of(ptr, sample, member)                                                                               \
	(__extension__({                                                                                                           \
		std::remove_reference<decltype(sample)>::type::Listeners* container = wl_container_of(ptr, container, member);         \
		container->parent;                                                                                                     \
	}))

#endif
