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
//   there is no process to exit from here, and exit() simply halts the chip -- which also ends
// the USB stack serving the 1200-baud reset, leaving a board that can only be recovered with
// the physical BOOT button. Dropping into BOOTSEL instead keeps it reflashable, and by this
// point light_fatal() has already flushed its explanation to the console
extern void light_core_port_abort(void);
#define light_platform_abort() light_core_port_abort()
// PWM blocks ("slices" in RP2 terms), and DMA paced by a DMA timer for streaming duty values
// into one -- see light_core_chip_rp2_common
#define LIGHT_PLATFORM_HAS_PWM 1
#define LIGHT_PLATFORM_HAS_PWM_STREAM 1

#endif
