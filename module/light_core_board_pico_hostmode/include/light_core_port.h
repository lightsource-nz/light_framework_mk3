#ifndef _LIGHT_CORE_PORT_H
#define _LIGHT_CORE_PORT_H

#include <stdlib.h>
#include <stdint.h>

#include <pico.h>
#include <pico/critical_section.h>

#define __static_descriptor __in_flash(".descriptors")
#define __static_buffer
#define __static_module __attribute__ ((section(".light.static_module")))
#define __static_stream __attribute__ ((section(".light.static_stream")))
#define __static_object __attribute__ ((section(".light.static")))

// C11 atomics are not supported on Cortex-M0/M0+ CPU cores, so RP2040 targets
// must use hard spinlocks for synchronization between cores
typedef uint32_t light_ref_t;

typedef critical_section_t light_mutex_t;
typedef uint32_t light_task_t;

struct light_object_registry {
        light_mutex_t *mutex;
        void *(*alloc)(size_t);
        void (*free)(void *);
};

#endif