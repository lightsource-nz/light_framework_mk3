#ifndef _LIGHT_PLATFORM_PORT_H
#define _LIGHT_PLATFORM_PORT_H

#define LIGHT_PLATFORM_HAS_STDERR 0
#define LIGHT_PLATFORM_HAS_C11_THREADS 0
// single-core, same as the F411 port
#define LIGHT_PLATFORM_HAS_MULTICORE_WORKER 0
//   same 128K SRAM budget as the F411 port (this chip's memory map is the same size, just with
// more peripherals behind it), so the same message-queue sizing derivation applies unchanged --
// see that port's light_platform_port.h for the full reasoning
#define LIGHT_STREAM_MQUEUE_DEPTH       24
#define LIGHT_STREAM_MAX_MSG_LENGTH     160

//   PWM via the timers, implemented in light_core_chip_stm32_common. Nothing on this board
// drives PWM yet, so the pin table there falls through to the generic empty case -- populate
// it there (following the F411 port's TIM1 table as an example) before setting this board up
// to use it.
#define LIGHT_PLATFORM_HAS_PWM 1
#define LIGHT_PLATFORM_HAS_PWM_STREAM 0

#endif
