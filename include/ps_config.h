#ifndef PS_CONFIG_H
#define PS_CONFIG_H

/* Compile-time feature flags (0 = disabled, 1 = enabled) */
#define PS_ENABLE_WITH 0
#define PS_ENABLE_EVAL 0
#define PS_ENABLE_ARGUMENTS_ALIASING 0
#ifndef PS_ENABLE_SDL
#define PS_ENABLE_SDL 1
#endif

#endif /* PS_CONFIG_H */
