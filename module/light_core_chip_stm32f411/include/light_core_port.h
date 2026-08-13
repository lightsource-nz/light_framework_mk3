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
// pico-sdk defines this for the RP2 ports; on a bare-CMSIS target there is no vendor header
// that would, so it is defined here. a real __packed __aligned(4) rather than the host build's
// empty definition, since this one describes structures that are actually laid out in flash
#define __packed_aligned __attribute__ ((packed, aligned(4)))

// Cortex-M4 has LDREX/STREX and therefore real lock-free C11 atomics, unlike RP2040's M0+.
// this port still uses a plain uint32_t guarded by the registry's critical section, matching
// light_core_chip_rp2350's reasoning: light_ref_t is not on any hot path here, and one proven
// implementation shared across the embedded ports is worth more than a faster divergent one
typedef uint32_t light_ref_t;

//   there is no OS on this target and only one core, so a mutex is exactly "do not let an
// interrupt handler observe this half-updated". PRIMASK is saved and restored rather than
// unconditionally re-enabled, so nesting works and a lock taken inside an already-critical
// region does not silently open an interrupt window on release.
//   this is a full interrupt disable, not a peripheral spinlock as on RP2: with one core there
// is nothing else to arbitrate against, and BASEPRI-based masking would be finer but requires
// a priority policy the framework does not yet have.
typedef uint32_t light_mutex_t;
typedef uint32_t light_task_t;
// no scheduler to block on, so light_condition_wait() spin-polls a plain flag -- see
// light_core_port_condition_wait(). every write happens under the associated mutex, and every
// caller re-checks its own predicate around the wait, which condition-variable usage requires
// on any platform anyway
typedef volatile bool light_condition_t;

extern void light_core_port_mutex_lock(light_mutex_t *mutex);
extern void light_core_port_mutex_unlock(light_mutex_t *mutex);

#define light_mutex_init(mutex) (*(mutex) = 0)
#define light_mutex_destroy(mutex) ((void)0)
// the same primitive: an interrupt disable is inherently recursive once PRIMASK is saved
#define light_mutex_init_recursive(mutex) (*(mutex) = 0)
#define light_mutex_do_lock(mutex) light_core_port_mutex_lock(mutex)
#define light_mutex_do_unlock(mutex) light_core_port_mutex_unlock(mutex)
#define light_condition_init(cond) (*(cond) = false)
#define light_condition_destroy(cond) ((void)0)
#define light_condition_signal(cond) (*(cond) = true)
#define light_condition_broadcast(cond) (*(cond) = true)
#define light_condition_wait(cond, mutex) light_core_port_condition_wait(cond, mutex)
// no bare-metal wait-with-timeout primitive yet; degrades to a plain wait, as on RP2350
#define light_condition_timedwait(cond, mutex, time) light_core_port_condition_wait(cond, mutex)

extern void light_core_port_condition_wait(light_condition_t *cond, light_mutex_t *mutex);

// brings up the USART console that newlib's _write() (and therefore every printf(), and
// therefore light_core's whole stream path) writes to. Called from light_platform_init()
// before anything can log -- see console.c
extern void light_core_port_console_init(void);

struct light_object_registry {
        light_mutex_t mutex;
        void *(*alloc)(size_t);
        void (*free)(void *);
};

#endif
