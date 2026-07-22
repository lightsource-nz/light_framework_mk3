#ifndef _LIGHT_CORE_PORT_H
#define _LIGHT_CORE_PORT_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include <pico.h>
#include <pico/critical_section.h>
#include <pico/platform.h>

#define __static_descriptor __in_flash(".descriptors")
#define __static_buffer
#define __static_module __attribute__ ((section(".light.static_module")))
#define __static_stream __attribute__ ((section(".light.static_stream")))
#define __static_object __attribute__ ((section(".light.static")))
// __packed_aligned is already defined by pico-sdk's own <pico/platform.h> (as a real
// __packed __aligned(4), unlike the empty host-build definition) -- unlike the host_os/
// pico_hostmode ports, this one must NOT redefine it

// C11 atomics are not supported on Cortex-M0/M0+ CPU cores, so RP2040 targets
// must use hard spinlocks for synchronization between cores
typedef uint32_t light_ref_t;

typedef critical_section_t light_mutex_t;
typedef uint32_t light_task_t;
// there is no OS scheduler on bare-metal RP2040, so there's nothing to block a core on --
// light_condition_wait() spin-polls a plain flag instead of doing a true blocking wait (no
// C11 atomics here either, same reasoning as light_ref_t above: every write to the flag happens
// while the caller already holds the associated light_mutex_t, per light_condition_signal()'s
// contract, and every read happens either under that same lock or during the busy-spin below,
// where a plain volatile read/write of a single-byte flag is enough on this cache-coherent
// hardware -- correctness doesn't depend on the flag alone anyway: every caller (see
// light_core/src/stream.c) re-checks its own predicate in a loop around the wait, which is
// required condition-variable usage on any platform since it must tolerate spurious/coalesced
// wakeups
typedef volatile bool light_condition_t;

#define light_mutex_init(mutex) critical_section_init(mutex)
#define light_mutex_destroy(mutex) critical_section_deinit(mutex)
#define light_mutex_init_recursive(mutex) critical_section_init(mutex)
#define light_mutex_do_lock(mutex) critical_section_enter_blocking(mutex)
#define light_mutex_do_unlock(mutex) critical_section_exit(mutex)
#define light_condition_init(cond) (*(cond) = false)
#define light_condition_destroy(cond) ((void)0)
#define light_condition_signal(cond) (*(cond) = true)
#define light_condition_broadcast(cond) (*(cond) = true)
#define light_condition_wait(cond, mutex) light_core_port_condition_wait(cond, mutex)
// no bare-metal wait-with-timeout primitive is implemented yet; degrades to a plain wait
#define light_condition_timedwait(cond, mutex, time) light_core_port_condition_wait(cond, mutex)

extern void light_core_port_condition_wait(light_condition_t *cond, light_mutex_t *mutex);

// runs 'worker_fn' on the second CPU core (see light_core/src/stream.c's use of
// LIGHT_PLATFORM_HAS_MULTICORE_WORKER); worker_fn must call light_core_port_worker_signal_finished()
// just before it returns, so that light_core_port_worker_join() can wait for it to actually stop
extern void light_core_port_worker_launch(void (*worker_fn)(void));
extern void light_core_port_worker_signal_stop(void);
extern bool light_core_port_worker_stop_requested(void);
extern void light_core_port_worker_signal_finished(void);
extern void light_core_port_worker_join(void);

struct light_object_registry {
        critical_section_t mutex;
        void *(*alloc)(size_t);
        void (*free)(void *);
};

#endif