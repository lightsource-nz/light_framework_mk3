#ifndef _LIGHT_CORE_PORT_H
#define _LIGHT_CORE_PORT_H

#include <stdlib.h>
#include <stdint.h>

#define __static_descriptor
#define __static_buffer
#define __static_module __attribute__ ((section(".light.static_module")))
#define __static_object __attribute__ ((section(".light.static")))

// C11 atomics are not reliably lock-free on Cortex-M0/M0+ CPU cores (GCC reports
// __GCC_ATOMIC_CHAR32_T_LOCK_FREE == 1, "sometimes", meaning a runtime fallback may be
// needed), so targets using these core architectures must provide a means of
// synchronization at the chip level instead. Checking this compiler-builtin macro
// directly (rather than `#ifndef atomic_char32_t`, the previous check here) is required
// because #ifndef can only ever see preprocessor macros, never the C11 atomic_char32_t
// *typedef* itself -- the old check could only "correctly" report no support by
// coincidence, on a target that also happened to not provide any macro-based fallback for
// the name. It could never actually detect genuine typedef'd support, which is how every
// modern target (including Cortex-M33, where GCC reports
// __GCC_ATOMIC_CHAR32_T_LOCK_FREE == 2, always lock-free) actually provides it -- so the
// old check unconditionally failed on any such target, not just ones that truly lack
// atomics
#include <stdatomic.h>
#if !defined(__GCC_ATOMIC_CHAR32_T_LOCK_FREE) || __GCC_ATOMIC_CHAR32_T_LOCK_FREE < 2
#       error "arm32_m driver requires a CPU core with atomic memory access instructions"
#endif
typedef atomic_char32_t light_ref_t;
#define __packed_aligned

struct light_object_registry {
        void *(*alloc)(size_t);
        void (*free)(void *);
};

#endif