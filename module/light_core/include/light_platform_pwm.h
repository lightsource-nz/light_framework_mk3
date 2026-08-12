#ifndef _LIGHT_PLATFORM_PWM_H
#define _LIGHT_PLATFORM_PWM_H

#include <stdint.h>
#include <stdbool.h>

#include <light_platform_port.h>

// PWM output, abstracted so that device libraries driving one -- a backlight, a buzzer, a
// motor -- contain no chip-specific code. Before this, every such driver carried its own
// `#if(LIGHT_SYSTEM == SYSTEM_PICO_SDK)` block with the same slice derivation and the same
// config dance inside it, plus an empty `#else` so host builds would still link.
//
// This lives in light_platform rather than in light_ioport because light_ioport abstracts BUS
// TRANSPORTS around an io_context with send-command/send-data semantics, and a PWM output has
// no command/data protocol to express through that. A PWM is an on-chip peripheral clocked
// off the system clock, which is the sort of thing light_platform already deals in.
//
// Availability is LIGHT_PLATFORM_HAS_PWM. Note that it is #defined to a VALUE and must be
// tested with #if, not #ifdef -- ports that lack the feature define it as 0, so #ifdef would
// report it present everywhere.

struct lp_pwm;

// claims `pin` as a PWM output. returns NULL if the pin has no PWM capability, its block is
// already claimed, or the platform has no PWM at all -- and callers MUST handle NULL, because
// on a host build that is the normal answer rather than an error
extern struct lp_pwm *light_platform_pwm_open(uint8_t pin);
// releases the block. does not change the pin, which is left however the last call put it;
// use light_platform_pwm_release_pin() first if the pin's resting level matters
extern void light_platform_pwm_close(struct lp_pwm *pwm);

// one period is clkdiv * (wrap + 1) system clocks, so the frequency is sys_clk over that, and
// `wrap` is simultaneously the duty resolution. the two are taken separately rather than as a
// frequency because callers care which resolution they get: light_backlight wraps at exactly
// its level maximum so that a level IS a duty value, with no rescaling anywhere
extern void light_platform_pwm_configure(struct lp_pwm *pwm, uint16_t wrap, uint8_t clkdiv);
// configures for a target frequency at the given resolution, returning the frequency actually
// achieved. the divider is an integer, so the result is only as close as that allows -- and
// integer rather than fractional on purpose, so an FPU-less part pays no more than any other
extern uint32_t light_platform_pwm_set_frequency(struct lp_pwm *pwm, uint32_t hz, uint16_t wrap);
// `duty` is in the same units as the configured wrap: 0 is always off, wrap is always full
extern void light_platform_pwm_set_duty(struct lp_pwm *pwm, uint16_t duty);
extern void light_platform_pwm_set_enabled(struct lp_pwm *pwm, bool enabled);
// hands the pin back to plain GPIO, driven to `level`. it takes a level rather than simply
// releasing because a floating pin is not a silent one -- a piezo across one picks up
// whatever its neighbours are doing
extern void light_platform_pwm_release_pin(struct lp_pwm *pwm, bool level);

// which hardware block the pin landed on. exposed only so a driver can log it: PWM blocks are
// shared between pins, two devices landing on one fight over wrap and clkdiv, and the symptom
// of that -- a backlight flickering in time with audio -- points nowhere near the cause
extern uint8_t light_platform_pwm_get_group(const struct lp_pwm *pwm);

#if LIGHT_PLATFORM_HAS_PWM_STREAM
// --- PWM as a DAC ---
//
// plays `count` 8-bit duty samples at `sample_rate`, without CPU involvement where the
// platform can manage it.
//
// this RECONFIGURES the PWM into 8-bit DAC mode -- undivided clock, wrapping at 255, which
// puts the carrier far above audible -- and so overrides any previous configure(). That
// carrier is inherent to the mechanism rather than a caller's choice, and requiring the
// caller to set it up would mean asking it to know precisely what this exists to insulate it
// from.
//
// `duty` is NOT copied and must stay valid until busy() goes false. That is what lets a
// pre-baked asset play straight out of flash at no cost in RAM
extern bool light_platform_pwm_stream_start(struct lp_pwm *pwm, const uint8_t *duty,
                                        uint32_t count, uint32_t sample_rate);
extern bool light_platform_pwm_stream_busy(const struct lp_pwm *pwm);
// stops early. leaves the output at mid-scale rather than wherever the last sample fell, so a
// transducer settles at zero average instead of being held off-centre
extern void light_platform_pwm_stream_stop(struct lp_pwm *pwm);
#endif

#endif
