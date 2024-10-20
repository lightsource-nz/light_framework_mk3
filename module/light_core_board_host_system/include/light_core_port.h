#ifndef _LIGHT_CORE_PORT_H
#define _LIGHT_CORE_PORT_H

#include <stdlib.h>
#include <stdint.h>

// at least as of now, all host platforms can be assumed to have hardware atomics
// and an implementation of at least the C11 atomic types
#include <stdatomic.h>
typedef atomic_char32_t light_ref_t;
#define __packed_aligned
#define __static_descriptor

struct light_object_registry {
        void *(*alloc)(size_t);
        void (*free)(void *);
};

#endif