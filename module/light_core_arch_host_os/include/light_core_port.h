#ifndef _LIGHT_CORE_PORT_H
#define _LIGHT_CORE_PORT_H

#include <stdlib.h>
#include <stdint.h>
#include <threads.h>

#define __static_descriptor
#define __static_buffer
#define __static_module __attribute__ ((section(".light.static_module")))
#define __static_stream __attribute__ ((section(".light.static_stream")))
#define __static_object __attribute__ ((section(".light.static")))

// at least as of now, all host platforms can be assumed to have hardware atomics
// and an implementation of at least the C11 atomic types
#include <stdatomic.h>
typedef atomic_char32_t light_ref_t;
#define __packed_aligned
#define __static_descriptor

typedef mtx_t light_mutex_t;
typedef thrd_t light_task_t;
typedef cnd_t light_condition_t;

#define light_mutex_init(mutex) mtx_init(mutex, mtx_plain)
#define light_mutex_destroy(mutex) mtx_destroy(mutex)
#define light_mutex_init_recursive(mutex) mtx_init(mutex, mtx_recursive)
#define light_mutex_do_lock(mutex) mtx_lock(mutex)
#define light_mutex_do_unlock(mutex) mtx_unlock(mutex)
#define light_condition_init(cond) cnd_init(cond)
#define light_condition_destroy(cond) cnd_destroy(cond)
#define light_condition_wait(cond, mutex) cnd_wait(cond, mutex)
#define light_condition_timedwait(cond, mutex, time) cnd_timedwait(cond, mutex, time)
#define light_condition_broadcast(cond) cnd_broadcast(cond)
#define light_condition_signal(cond) cnd_signal(cond)

struct light_object_registry {
        void *(*alloc)(size_t);
        void (*free)(void *);
};

#endif