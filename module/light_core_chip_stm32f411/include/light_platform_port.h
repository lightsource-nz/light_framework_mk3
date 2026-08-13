#ifndef _LIGHT_PLATFORM_PORT_H
#define _LIGHT_PLATFORM_PORT_H

// no host stdio to split streams across: light_stream_stderr and light_stream_stdout both end
// up wherever this board's console goes
#define LIGHT_PLATFORM_HAS_STDERR 0
#define LIGHT_PLATFORM_HAS_C11_THREADS 0
// STM32F411 is single-core. the message-stream worker has no second core to run on, so
// light_stream_setup() takes its synchronous drain path -- see the note on
// LIGHT_STREAM_MQUEUE_DEPTH in light_stream.h, which is why that queue is sized for the whole
// boot sequence rather than for steady-state logging
#define LIGHT_PLATFORM_HAS_MULTICORE_WORKER 0
// no PWM through light_platform yet. light_platform_pwm_open() is absent rather than stubbed,
// and the device libraries that use it create no device -- the same path a board with no
// buzzer already takes, so nothing downstream needs a special case. TIM1-5/9-11 are all here
// when this is worth writing
#define LIGHT_PLATFORM_HAS_PWM 0
#define LIGHT_PLATFORM_HAS_PWM_STREAM 0

#endif
