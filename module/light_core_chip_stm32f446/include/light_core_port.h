#ifndef _LIGHT_CORE_PORT_H
#define _LIGHT_CORE_PORT_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include <stm32f4xx.h>

#define __static_descriptor
#define __static_buffer
#define __static_module __attribute__ ((used, section(".light.static_module")))
#define __static_stream __attribute__ ((used, section(".light.static_stream")))
#define __static_object __attribute__ ((used, section(".light.static")))
#define __packed_aligned __attribute__ ((packed, aligned(4)))

// Cortex-M4, same reasoning as the F411 port: real LDREX/STREX atomics exist, but light_ref_t
// still goes through the registry's critical section rather than a divergent lock-free path
typedef uint32_t light_ref_t;

typedef uint32_t light_mutex_t;
typedef uint32_t light_task_t;
typedef volatile bool light_condition_t;

extern void light_core_port_mutex_lock(light_mutex_t *mutex);
extern void light_core_port_mutex_unlock(light_mutex_t *mutex);

#define light_mutex_init(mutex) (*(mutex) = 0)
#define light_mutex_destroy(mutex) ((void)0)
#define light_mutex_init_recursive(mutex) (*(mutex) = 0)
#define light_mutex_do_lock(mutex) light_core_port_mutex_lock(mutex)
#define light_mutex_do_unlock(mutex) light_core_port_mutex_unlock(mutex)
#define light_condition_init(cond) (*(cond) = false)
#define light_condition_destroy(cond) ((void)0)
#define light_condition_signal(cond) (*(cond) = true)
#define light_condition_broadcast(cond) (*(cond) = true)
#define light_condition_wait(cond, mutex) light_core_port_condition_wait(cond, mutex)
#define light_condition_timedwait(cond, mutex, time) light_core_port_condition_wait(cond, mutex)

extern void light_core_port_condition_wait(light_condition_t *cond, light_mutex_t *mutex);

extern void light_core_port_console_init(void);

struct light_object_registry {
        light_mutex_t mutex;
        void *(*alloc)(size_t);
        void (*free)(void *);
};

#endif
