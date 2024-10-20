#ifndef _LIGHT_CORE_PORT_H
#define _LIGHT_CORE_PORT_H

#include <stdlib.h>
#include <stdint.h>

// C11 atomics are not supported on Cortex-M0/M0+ CPU cores, so targets using
// these core architectures must provide a means of synchronization at the chip
// level
#include <stdatomic.h>
#ifndef atomic_char32_t
#       error "arm32_m driver requires a CPU core with atomic memory access instructions"
#endif
typedef atomic_char32_t light_ref_t;
#define __packed_aligned
#define __static_descriptor

struct light_object_registry {
        void *(*alloc)(size_t);
        void (*free)(void *);
};

#endif