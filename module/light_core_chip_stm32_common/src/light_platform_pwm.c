#include <light.h>
#include <light_platform.h>

//   No PWM through light_platform on the STM32 ports yet, so these are the same honest stubs
// the host port provides: open() returns NULL, and the device libraries that use it then
// create no device -- the path a board with no backlight or no buzzer already takes.
//
//   They have to EXIST, which is the part the port headers originally got wrong by saying
// light_platform_pwm_open() was "absent rather than stubbed". The declarations in
// light_platform_pwm.h are not guarded by LIGHT_PLATFORM_HAS_PWM -- that is the whole point of
// the abstraction, so consumers need no #if of their own -- so a build linking light_backlight
// or light_audio fails at LINK time with six undefined references, naming light_platform
// rather than anything that would lead back to here. LIGHT_PLATFORM_HAS_PWM stays 0, for code
// that wants to skip the work rather than call into no-ops.
//
//   Both STM32 parts have plenty of timers (TIM1-8, 12-17 on the H743), so this is a gap to
// fill rather than a limitation to work around, and it is shared between the chip ports
// because the timer peripheral is common to both families.

struct lp_pwm *light_platform_pwm_open(uint8_t pin)
{
        light_debug("no PWM on this platform yet; ignoring request for pin 0x%x", pin);
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
