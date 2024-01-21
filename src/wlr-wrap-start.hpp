#ifdef WLROOTS_INCLUDE_WRAP_STARTED
static_assert(0 == 1, "wlroots include wrap started and not ended");
#endif

#define WLROOTS_INCLUDE_WRAP_STARTED

#include <pthread.h>
#include <stdlib.h>
#include <wayland-server-core.h>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif

#define class _class
#define namespace _namespace
#define static

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#define WLR_USE_UNSTABLE

extern "C" {
