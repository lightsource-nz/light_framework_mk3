#ifndef _LIGHT_CORE_PORT_H
#define _LIGHT_CORE_PORT_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include <stm32h7xx.h>

#define __static_descriptor
#define __static_buffer
#define __static_module __attribute__ ((used, section(".light.static_module")))
#define __static_stream __attribute__ ((used, section(".light.static_stream")))
#define __static_object __attribute__ ((used, section(".light.static")))
#define __packed_aligned __attribute__ ((packed, aligned(4)))

// Cortex-M7 has LDREX/STREX and real lock-free C11 atomics. As on the F411 port, light_ref_t
// is a plain uint32_t guarded by the registry's critical section instead: it is not on any hot
// path, and one implementation shared across the embedded ports is worth more than a faster
// divergent one
typedef uint32_t light_ref_t;

//   identical to the F411 port's, and deliberately so -- the implementation lives in
// light_core_chip_stm32_common and is CMSIS-core only. PRIMASK is saved into the mutex and
// restored on unlock so that nesting works: a lock taken inside an existing critical region
// restores "still masked" rather than opening an interrupt window the outer region was
// relying on being shut.
//   Single-core, so there is nothing to arbitrate against and no hardware spinlock is needed.
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

// brings up the USART console that newlib's _write() writes to, and therefore every printf()
// and light_core's whole stream path. Called from light_platform_init() before anything can
// log -- see this port's console.c
extern void light_core_port_console_init(void);

struct light_object_registry {
        light_mutex_t mutex;
        void *(*alloc)(size_t);
        void (*free)(void *);
};

#endif
