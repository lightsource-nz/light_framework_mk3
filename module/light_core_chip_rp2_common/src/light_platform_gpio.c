/*
 *  light_platform_gpio.c -- discrete output pins on RP2.
 *
 *  In rp2_common rather than in each chip port, for the same reason light_platform_pwm.c is:
 *  the SDK's GPIO API is identical on RP2040 and RP2350, so two copies would only be two places
 *  to fix a bug in.
 *
 *  This is deliberately not a general GPIO abstraction -- see the note in light_platform.h.
 *  Anything that needs a pin as part of a transport goes through light_ioport, which owns pin
 *  function selection for the peripheral it is driving.
 */
#include <light.h>
#include <light_platform.h>

#include <hardware/gpio.h>

void light_platform_gpio_configure_output(uint32_t pin, bool initial)
{
        //   the initial level is set BEFORE the direction. Reversed, the pin drives its reset
        // state -- low -- for the instant between the two calls, which is a visible flash on an
        // LED and a genuine glitch on anything that treats the line as an enable.
        gpio_init(pin);
        gpio_put(pin, initial);
        gpio_set_dir(pin, GPIO_OUT);
}

void light_platform_gpio_write(uint32_t pin, bool value)
{
        gpio_put(pin, value);
}
