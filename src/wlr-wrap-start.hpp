#ifdef WLROOTS_INCLUDE_WRAP_STARTED
static_assert(0 == 1, "wlroots include wrap started and not ended");
#endif

#define WLROOTS_INCLUDE_WRAP_STARTED

#include <pthread.h>
#define class _class
#define namespace _namespace
#define static
#define WLR_USE_UNSTABLE

extern "C" {
