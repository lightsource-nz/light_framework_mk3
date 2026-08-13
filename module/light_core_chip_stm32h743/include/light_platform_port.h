#ifndef _LIGHT_PLATFORM_PORT_H
#define _LIGHT_PLATFORM_PORT_H

#define LIGHT_PLATFORM_HAS_STDERR 0
#define LIGHT_PLATFORM_HAS_C11_THREADS 0
// STM32H743 is single-core (the dual-core part is the H745/H747). No second core for the
// message-stream worker, so light_stream_setup() takes its synchronous drain path
#define LIGHT_PLATFORM_HAS_MULTICORE_WORKER 0
//   PWM via the timers, implemented in light_core_chip_stm32_common. Pin-to-timer is a table
// there rather than a formula -- the STM32 mapping is an arbitrary matrix per part -- so a pin
// outside it returns NULL from open(), which every consumer already handles.
//   No streaming (PWM-as-DAC): that needs DMA paced into the compare register, which nothing
// on these boards has asked for yet.
#define LIGHT_PLATFORM_HAS_PWM 1
#define LIGHT_PLATFORM_HAS_PWM_STREAM 0

//   the queue-sizing defaults are LEFT ALONE here, unlike on the F411. This part has 1MB of
// RAM across its domains and 512KB in the AXI SRAM this port uses as main RAM, so the 33KB the
// two streams cost is under 7% of it -- not worth trading against the deadlock risk that
// shrinking the depth carries on a single-core drain path. See the sizing note in
// light_stream.h, and light_core_chip_stm32f411/include/light_platform_port.h for the case
// where it IS worth it (128KB total, where the same 33KB is 26%).

#endif
