#ifndef PS_CONFIG_H
#define PS_CONFIG_H

/* Compile-time feature flags (0 = disabled, 1 = enabled) */

#ifndef PS_ENABLE_WITH
#define PS_ENABLE_WITH 1
#endif

#ifndef PS_ENABLE_EVAL
#define PS_ENABLE_EVAL 1
#endif

#ifndef PS_ENABLE_ARGUMENTS_ALIASING
#define PS_ENABLE_ARGUMENTS_ALIASING 1
#endif

#ifndef PS_ENABLE_UNICODE_IDENTIFIERS
#define PS_ENABLE_UNICODE_IDENTIFIERS 1
#endif

#ifndef PS_ENABLE_FAST_CALLS
#define PS_ENABLE_FAST_CALLS 1
#endif

#ifndef PS_ENABLE_PERF
#define PS_ENABLE_PERF 1
#endif

#ifndef PS_DISABLE_PEEPHOLE
#define PS_DISABLE_PEEPHOLE 0
#endif

#ifndef PS_DISABLE_CF_FUSIONS
#define PS_DISABLE_CF_FUSIONS 0
#endif

#ifndef PS_ENABLE_MODULE_DISPLAY
#define PS_ENABLE_MODULE_DISPLAY 1
#endif

#ifndef PS_ENABLE_MODULE_FS
#define PS_ENABLE_MODULE_FS 1
#endif

#ifndef PS_ENABLE_MODULE_IMG
#define PS_ENABLE_MODULE_IMG 1
#endif

#ifndef PS_IMG_MAX_IMAGES
#define PS_IMG_MAX_IMAGES 8
#endif

#ifndef PS_IMG_MAX_WIDTH
#define PS_IMG_MAX_WIDTH 7680
#endif

#ifndef PS_IMG_MAX_HEIGHT
#define PS_IMG_MAX_HEIGHT 4320
#endif

#ifndef PS_EVENT_QUEUE_CAPACITY
#define PS_EVENT_QUEUE_CAPACITY 64
#endif

#endif /* PS_CONFIG_H */
