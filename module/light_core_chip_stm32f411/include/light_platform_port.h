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
//   MESSAGE QUEUE SIZING. The defaults (64 x 257 bytes, per stream, two streams) cost 33KB of
// .data -- 26% of this part's 128KB of RAM, spent before an application has allocated
// anything. That is invisible on RP2350's 520KB and is the single largest consumer here, so
// the RP2 ports keep the defaults and this one does not.
//   The depth is the number that has to be got right, because LIGHT_PLATFORM_HAS_MULTICORE_WORKER
// is 0 above: a full queue deadlocks rather than drops. What must fit is the longest run
// between drains, which is framework init through _load_static_objects() -- see the sizing note
// in light_stream.h. Measured on demo_blackpill that segment is 10 messages, so 24 leaves rather
// more than twice the headroom actually needed.
//   AN APPLICATION THAT LOGS MORE DURING HARDWARE INIT MUST RAISE THIS. A board bringing up a
// display, touch, IMU and audio pushes that segment to ~25 messages and would hang here. Set it
// per-application with target_compile_definitions() if so.
//   The message length is the safer of the two to trim, since it truncates rather than hangs.
// The longest line either demo emits is ~105 characters, so 160 keeps a comfortable margin.
//   Net: 24 * 161 * 2 = 7.7KB, against 33KB.
#define LIGHT_STREAM_MQUEUE_DEPTH       24
#define LIGHT_STREAM_MAX_MSG_LENGTH     160

// no PWM through light_platform yet. light_platform_pwm_open() is absent rather than stubbed,
// and the device libraries that use it create no device -- the same path a board with no
// buzzer already takes, so nothing downstream needs a special case. TIM1-5/9-11 are all here
// when this is worth writing
#define LIGHT_PLATFORM_HAS_PWM 0
#define LIGHT_PLATFORM_HAS_PWM_STREAM 0

#endif
