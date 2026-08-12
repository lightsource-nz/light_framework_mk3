#include <light.h>
#include <light_platform.h>

// there is no PWM on a host build, and pretending otherwise would be worse than admitting it:
// light_platform_pwm_open() returns NULL, the device libraries that use it then create no
// device, and the demo applications already take exactly that path for a board with no
// backlight or no buzzer. So a host build stays a link-and-logic check rather than becoming a
// simulator that has to be kept honest.
//
// The functions still exist so that consumers need no #if of their own -- which is the whole
// point of the abstraction. LIGHT_PLATFORM_HAS_PWM is 0 here for code that wants to skip the
// work entirely rather than call into no-ops.

struct lp_pwm *light_platform_pwm_open(uint8_t pin)
{
        light_debug("no PWM on this platform; ignoring request for pin %d", pin);
        return NULL;
}
void light_platform_pwm_close(struct lp_pwm *pwm) { (void)pwm; }
void light_platform_pwm_configure(struct lp_pwm *pwm, uint16_t wrap, uint8_t clkdiv)
{
        (void)pwm; (void)wrap; (void)clkdiv;
}
uint32_t light_platform_pwm_set_frequency(struct lp_pwm *pwm, uint32_t hz, uint16_t wrap)
{
        (void)pwm; (void)hz; (void)wrap;
        // 0 rather than the requested frequency: nothing is oscillating, and a caller that
        // logs what it got should not be told it got what it asked for
        return 0;
}
void light_platform_pwm_set_duty(struct lp_pwm *pwm, uint16_t duty) { (void)pwm; (void)duty; }
void light_platform_pwm_set_enabled(struct lp_pwm *pwm, bool enabled) { (void)pwm; (void)enabled; }
void light_platform_pwm_release_pin(struct lp_pwm *pwm, bool level) { (void)pwm; (void)level; }
uint8_t light_platform_pwm_get_group(const struct lp_pwm *pwm) { (void)pwm; return 0; }
