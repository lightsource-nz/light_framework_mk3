#ifndef _LIGHT_PLATFORM_PORT_H
#define _LIGHT_PLATFORM_PORT_H

#define LIGHT_PLATFORM_HAS_STDERR 0
#define LIGHT_PLATFORM_HAS_C11_THREADS 0
// RP2350 has no OS threads, but it does have a second physical CPU core: the background
// message-stream worker runs there instead (see light_core_port_worker_launch() below and its
// use in light_core/src/stream.c). overridable per-app (e.g. via target_compile_definitions)
// for boards where this collides with something else running on core1 -- see the USB CDC
// stdio case: TinyUSB's device stack isn't safe to call from more than one execution
// context, and pico_stdio_usb drives tud_task() via its own background IRQ independent of
// the app, so a printf() on core1 here can race that IRQ's own access to the same CDC
// buffers, corrupting/interleaving output
#ifndef LIGHT_PLATFORM_HAS_MULTICORE_WORKER
#define LIGHT_PLATFORM_HAS_MULTICORE_WORKER 1
#endif

#endif
