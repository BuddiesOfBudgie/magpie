#ifdef WLROOTS_INCLUDE_WRAP_STARTED
static_assert(0 == 1, "wlroots include wrap started and not ended");
#endif

#define WLROOTS_INCLUDE_WRAP_STARTED

#include <pthread.h>
#include <wayland-server-core.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#define class _class
#define namespace _namespace
#define static
#pragma clang diagnostic pop
#define WLR_USE_UNSTABLE

extern "C" {
