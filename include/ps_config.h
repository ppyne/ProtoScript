#ifndef PS_CONFIG_H
#define PS_CONFIG_H

/* Compile-time feature flags (0 = disabled, 1 = enabled) */
#define PS_ENABLE_WITH 0
#define PS_ENABLE_EVAL 0
#define PS_ENABLE_ARGUMENTS_ALIASING 0
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
