#ifndef _LIGHT_PLATFORM_PORT_H
#define _LIGHT_PLATFORM_PORT_H

// no host stdio to split streams across -- see the F411 port's rationale, unchanged here
#define LIGHT_PLATFORM_HAS_STDERR 0
#define LIGHT_PLATFORM_HAS_C11_THREADS 0
// single-core, same as every other bare-CMSIS port here
#define LIGHT_PLATFORM_HAS_MULTICORE_WORKER 0
//   MESSAGE QUEUE SIZING. The F411 port's defaults (24 x 160 bytes, per stream, two streams)
// already cost 7.7KB to keep this port's boot-time log traffic from deadlocking a queue with
// no second core to drain it -- see that port's light_platform_port.h for the full derivation.
// This board has only 20KB of RAM total, where 7.7KB is nearly 40% of it rather than the
// F411's few percent, so the same depth is kept ONLY because the demo here logs less than the
// F411's does (one line at boot, nothing after) -- there is no margin left over for an
// application that logs more during hardware init the way the note on that port warns about.
// Trim LIGHT_STREAM_MQUEUE_DEPTH first if a real application on this board needs the RAM back.
#define LIGHT_STREAM_MQUEUE_DEPTH       24
#define LIGHT_STREAM_MAX_MSG_LENGTH     160

//   PWM via the timers, implemented in light_core_chip_stm32_common -- but the F1's GPIO block
// has no per-pin alternate-function register (see that module's light_platform_pwm.c), so this
// chip's pin table is empty rather than populated: light_platform_pwm_open() compiles and
// links, and returns NULL for every pin, same as a board with nothing wired to PWM. Wire up
// AFIO_MAPR handling there before setting this to reflect real pins.
#define LIGHT_PLATFORM_HAS_PWM 1
#define LIGHT_PLATFORM_HAS_PWM_STREAM 0

#endif
