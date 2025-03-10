#ifndef _LIGHT_COMMON_H
#define _LIGHT_COMMON_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <assert.h>

#include <light_common_compiler.h>

#include <light_board.h>
#include <light_chip.h>
#include <light_arch.h>

#define _TO_STR(arg) #arg
#define TO_STR(arg) _TO_STR(arg)

// -- IMPORTANT - PLEASE NOTE --
// by default, we disable all logging and debug features, as these may create
// performance issues or security vulnerabilities if left enabled accidentally
// (n.b. although command-line mode defaults to generating info-level logging output)
#ifndef RUN_MODE
#define RUN_MODE PRODUCTION
#endif

#define MODE_PRODUCTION         0
#define MODE_TEST               1
#define MODE_DEVELOPMENT        2
#define MODE_DEBUG              3
#define MODE_TRACE              4

#define _GET_RUN_MODE(mode) MODE_## mode
#define GET_RUN_MODE(mode) _GET_RUN_MODE(mode)

#define LIGHT_RUN_MODE GET_RUN_MODE(RUN_MODE)

// production mode means everything is locked down tight,
// for maximum performance and security
#if (LIGHT_RUN_MODE == MODE_PRODUCTION)
#       ifndef FILTER_LOG_LEVEL
#               define FILTER_LOG_LEVEL DISABLE
#       endif
#       ifndef LIGHT_DEBUG_ENABLE
#               define LIGHT_DEBUG_ENABLE 0
#       endif
#       ifndef LIGHT_TRACE_ENABLE
#               define LIGHT_TRACE_ENABLE 0
#       endif
// testing mode means we want t0 report warnings and errors
// while keeping optimization at (hopefully) near-production levels
#elif (LIGHT_RUN_MODE == MODE_TEST)
#       ifndef FILTER_LOG_LEVEL
#               define FILTER_LOG_LEVEL WARN
#       endif
#       ifndef LIGHT_DEBUG_ENABLE
#               define LIGHT_DEBUG_ENABLE 0
#       endif
#       ifndef LIGHT_TRACE_ENABLE
#               define LIGHT_TRACE_ENABLE 0
#       endif
// development mode means we want readable and informative log messages,
// without noisy debug output
#elif (LIGHT_RUN_MODE == MODE_DEVELOPMENT)
#       ifndef FILTER_LOG_LEVEL
#               define FILTER_LOG_LEVEL INFO
#       endif
#       ifndef LIGHT_DEBUG_ENABLE
#               define LIGHT_DEBUG_ENABLE 0
#       endif
#       ifndef LIGHT_TRACE_ENABLE
#               define LIGHT_TRACE_ENABLE 0
#       endif
// debug mode - verbose diagnostic output, and
// enable sanity checking assertions
#elif (LIGHT_RUN_MODE == MODE_DEBUG)
#       ifndef FILTER_LOG_LEVEL
#               define FILTER_LOG_LEVEL DEBUG
#       endif
#       ifndef LIGHT_DEBUG_ENABLE
#               define LIGHT_DEBUG_ENABLE 1
#       endif
#       ifndef LIGHT_TRACE_ENABLE
#               define LIGHT_TRACE_ENABLE 0
#       endif
// trace mode - maximum logging verbosity
#elif (LIGHT_RUN_MODE == MODE_TRACE)
#       ifndef FILTER_LOG_LEVEL
#               define FILTER_LOG_LEVEL TRACE
#       endif
#       ifndef LIGHT_DEBUG_ENABLE
#               define LIGHT_DEBUG_ENABLE 1
#       endif
#       ifndef LIGHT_TRACE_ENABLE
#               define LIGHT_TRACE_ENABLE 1
#       endif
#endif

#define LOG_TRACE 4
#define LOG_DEBUG 3
#define LOG_INFO 2
#define LOG_WARN 1
#define LOG_ERROR 0
#define LOG_DISABLE -1

#define _GET_LOG_LEVEL(level) LOG_## level
#define GET_LOG_LEVEL(level) _GET_LOG_LEVEL(level)

#define LIGHT_MAX_LOG_LEVEL GET_LOG_LEVEL(FILTER_LOG_LEVEL)

// TODO tune these buffer sizes, and make them configurable
#define LIGHT_LOG_BUFFER_PRI_SIZE 128
#define LIGHT_LOG_BUFFER_SEC_SIZE 128

// TODO initially the default stream for INFO and lower levels is stdout, with WARN and ERROR going to stderr.
// but these defaults should probably be made configurable
#if (LIGHT_MAX_LOG_LEVEL >= LOG_TRACE)
#define light_trace(format, ...) light_log_internal(light_stream_stdout, LOG_TRACE, __func__, format __VA_OPT__(,) __VA_ARGS__)
#else
#define light_trace(format, ...) (void)format
#endif
#if (LIGHT_MAX_LOG_LEVEL >= LOG_DEBUG)
#define light_debug(format, ...) light_log_internal(light_stream_stdout, LOG_DEBUG, __func__, format __VA_OPT__(,) __VA_ARGS__)
#else
#define light_debug(format, ...) (void)format
#endif
#if (LIGHT_MAX_LOG_LEVEL >= LOG_INFO)
#define light_info(format, ...) light_log_internal(light_stream_stdout, LOG_INFO, __func__, format __VA_OPT__(,) __VA_ARGS__)
#else
#define light_info(format, ...) (void)format
#endif
#if (LIGHT_MAX_LOG_LEVEL >= LOG_WARN)
#define light_warn(format, ...) light_log_internal(light_stream_stderr, LOG_WARN, __func__, format __VA_OPT__(,) __VA_ARGS__)
#else
#define light_warn(format, ...) (void)format
#endif
#if (LIGHT_MAX_LOG_LEVEL >= LOG_ERROR)
#define light_error(format, ...) light_log_internal(light_stream_stderr, LOG_ERROR, __func__, format __VA_OPT__(,) __VA_ARGS__)
#else
#define light_error(format, ...) (void)format
#endif
#define light_fatal(format, ...) \
do { \
        light_log_internal(light_stream_stderr, LOG_ERROR, __func__, format __VA_OPT__(,) __VA_ARGS__); \
        exit(-1); \
} while(0)

// by default, assume a bare-metal target system
#ifndef SYSTEM
#define SYSTEM NONE
#endif

#define SYSTEM_NONE             0
#define SYSTEM_HOST_OS          1
#define SYSTEM_PICO_SDK         2
#define SYSTEM_FREERTOS         3

#define _GET_SYSTEM(system) SYSTEM_## system
#define GET_SYSTEM(system) _GET_SYSTEM(system)

#define LIGHT_SYSTEM GET_SYSTEM(SYSTEM)

// PLATFORM defines where code is to be executed: on target hardware, simulator,
// development host machine, etc.
// by default we build for the target embedded hardware platform
#ifndef PLATFORM
#define PLATFORM TARGET
#endif

#define PLATFORM_TARGET         0
#define PLATFORM_HOST           1
#define PLATFORM_EMULATOR       2

#define _GET_PLATFORM(platform) PLATFORM_## platform
#define GET_PLATFORM(platform) _GET_PLATFORM(platform)

#define LIGHT_PLATFORM GET_PLATFORM(PLATFORM)

#if(LIGHT_SYSTEM == SYSTEM_PICO_SDK)
#define __HAVE_PICO_SDK
#if(LIGHT_PLATFORM == PLATFORM_TARGET)
#define __HAVE_RP2_HW
#endif
#endif

#define LIGHT_BUILD_STRING "SYSTEM:" TO_STR(SYSTEM) " // PLATFORM:" TO_STR(PLATFORM) " // RUN_MODE:" TO_STR(RUN_MODE)

/*
// this section is subject to change as the framework's view of the host and target systems evolves.
// for now we just define SYSTEM and PLATFORM to give applications a broad view of their runtime context.

#define LIGHT_TARGET_STRING ( #TARGET_CHIP "//" #TARGET_ARCH "//" #TARGET_CORE )
#define LIGHT_HOST_STRING ( #HOST_SYSTEM "//" #HOST_ARCH )
*/

// type manipulation macros shamelessly borrowed from the Linux kernel
#define container_of(ptr, type, member) ({                          \
        void *__mptr = (void *)(ptr);                               \
        static_assert(__same_type(*(ptr), ((type *)0)->member) ||   \
                __same_type(*(ptr), void),                          \
                "pointer type mismatch in container_of()");         \
        ((type *)(__mptr - offsetof(type, member))); })

#ifndef __same_type
# define __same_type(a, b) __builtin_types_compatible_p(typeof(a), typeof(b))
#endif

#define LIGHT_OK                        (uint8_t) 0x0u          // indicates successful result
#define LIGHT_INVALID                   (uint8_t) 0x1u          // invalid argument or input
#define LIGHT_NO_MEMORY                 (uint8_t) 0x2u          // insufficient memory for operation
#define LIGHT_NO_RESOURCE               (uint8_t) 0x3u          // insufficient system resource for operation
#define LIGHT_STATE_INVALID             (uint8_t) 0x4u          // invalid internal state for operation
#define LIGHT_EXTERNAL                  (uint8_t) 0x5u          // error in external library or hardware

#define LIGHT_OPT_LOG_LEVEL             "LIGHT_LOG_LEVEL"
#define LIGHT_OPT_RUN_MODE              "LIGHT_RUN_MODE"

extern void light_common_init();
extern const uint8_t *light_error_to_string(uint8_t level);
extern const uint8_t *light_run_mode_to_string(uint8_t mode);
extern const uint8_t *light_log_level_to_string(uint8_t level);
extern void light_log_internal(struct light_stream *stream, const uint8_t level,const uint8_t *func, const uint8_t *format, ...);

// mapped to default malloc/free routines for SYSTEM
extern void *light_alloc(size_t size);
extern void light_free(void *obj);

int16_t _light_arraylist_indexof(void* (*list)[], uint8_t count, void *item);
void _light_arraylist_delete_at_index(void* (*list)[], uint8_t *count, uint8_t index);
void _light_arraylist_delete_item(void* (*list)[], uint8_t *count, void *item);
void _light_arraylist_append(void* (*list)[], uint8_t *count, void *item);
void _light_arraylist_insert(void* (*list)[], uint8_t *count, void *item, uint8_t index);

#define light_arraylist_indexof(list, count, item) _light_arraylist_indexof((void* (*)[]) list, count, (void *) item)
#define light_arraylist_delete_at_index(list, count, index) _light_arraylist_delete_at_index((void* (*)[]) list, count, index)
#define light_arraylist_delete_item(list, count, item) _light_arraylist_delete_item((void* (*)[]) list, count, (void *) item)
#define light_arraylist_append(list, count, item) _light_arraylist_append((void* (*)[]) list, count, (void *) item)
#define light_arraylist_insert(list, count, item, index) _light_arraylist_insert((void* (*)[]) list, count, (void *) item, index)

#endif