#include <light.h>
#if(LIGHT_SYSTEM != SYSTEM_PICO_SDK)
        #error "this file should only be compiled when Pico SDK support is enabled"
#endif

#include <pico/time.h>

#include <threads.h>
#include <unistd.h>
#include <stdlib.h>

#define LIGHT_PLATFORM_MAX_TIMERS       8
#define _INSTANCE_INVALID               16

#define TIMER_IDLE                      0
#define TIMER_RUN                       1

static struct lp_timer *timer_instance[LIGHT_PLATFORM_MAX_TIMERS];
static uint8_t timer_instance_count = 0;

light_task_t main_task;
static uint32_t system_time_at_init;

void light_platform_init()
{
        main_task = thrd_current();
        system_time_at_init = light_platform_get_absolute_time_ms();
}
light_task_t light_platform_get_task()
{
        return thrd_current();
}
light_task_t light_platform_get_main_task()
{
        return main_task;
}
static uint8_t _get_free_timer_instance()
{
        for(uint8_t i = 0; i < LIGHT_PLATFORM_MAX_TIMERS; i++) {
                if(timer_instance[i] == NULL)
                        return i;
        }
        return _INSTANCE_INVALID;
}
struct lp_timer *light_platform_timer_new()
{
        struct lp_timer *t = light_alloc(sizeof(*t));
        if((t->id = _get_free_timer_instance()) != _INSTANCE_INVALID) {
                timer_instance[t->id] = t;
                return t;
        }
        return NULL;
}
uint8_t light_platform_timer_set_ms(struct lp_timer *timer, uint32_t time_ms, bool start)
{
        if(timer->state == TIMER_IDLE) {
                timer->duration_ms = time_ms;
                if(start)
                        light_platform_timer_run(timer);
                return LIGHT_OK;
        }
        // ignore command if timer is not IDLE
        return LIGHT_STATE_INVALID;
}
void light_platform_timer_run(struct lp_timer *timer)
{
        if(timer->state == TIMER_IDLE) {
                timer->target_ms = to_ms_since_boot(delayed_by_ms(get_absolute_time(), timer->duration_ms));
                timer->state = TIMER_RUN;
        }
}
void light_platform_timer_stop(struct lp_timer *timer)
{
        if(timer->state == TIMER_RUN) {
                uint32_t now_ms = to_ms_since_boot(get_absolute_time());
                if(timer->target_ms <= now_ms) {
                        timer->duration_ms = 0;
                } else {
                        timer->duration_ms = timer->target_ms - now_ms;
                }
        }
}
uint32_t light_platform_timer_get_remaining_ms(struct lp_timer *timer)
{
        switch(timer->state) {
        case TIMER_IDLE:
                return timer->duration_ms;
        case TIMER_RUN:;
                //   two fixes here. The clock: target_ms is set from to_ms_since_boot() in
                // light_platform_timer_run() above, which is ABSOLUTE time, while this
                // compared it against time-since-init -- the two differ by however long the
                // framework took to start, so every remaining time was overstated by that much.
                //   and the arithmetic: `target < now` returning target - now underflows on
                // unsigned counters, so a timer that had just expired reported nearly
                // UINT32_MAX ms left. A signed difference is also wrap-safe, which comparing
                // the absolute values is not. Matches light_core_chip_stm32_common
                int32_t remaining = (int32_t)(timer->target_ms - light_platform_get_absolute_time_ms());
                return (remaining > 0) ? (uint32_t)remaining : 0;
        }
        //   every other state has nothing pending. Falling off the end was undefined behaviour:
        // the switch covers two states and the enum has more
        return 0;
}
uint32_t light_platform_get_absolute_time_ms()
{
        return to_ms_since_boot(get_absolute_time());
}
uint32_t light_platform_get_time_since_init()
{
        return light_platform_get_absolute_time_ms() - system_time_at_init;
}
void light_platform_sleep_ms(uint32_t period)
{
        sleep_ms(period);
}
static uint8_t *_do_getenv(const uint8_t *name)
{
#if defined(__GNUC__) && !defined(_WIN32)
        return secure_getenv(name);
#else
        return getenv(name);
#endif
}
uint8_t *light_platform_getenv(const uint8_t *name)
{
        return _do_getenv(name);
}
uint8_t *light_platform_get_user_home()
{
#ifdef _WIN32
        uint8_t *home = _do_getenv("HOME");
        return home ? home : _do_getenv("USERPROFILE");
#else
        return _do_getenv("HOME");
#endif
}
uint8_t *light_platform_get_user_name()
{
#ifdef _WIN32
        uint8_t *name = _do_getenv("USER");
        return name ? name : _do_getenv("USERNAME");
#else
        return _do_getenv("USER");
#endif
}
uint16_t light_platform_get_user_id()
{
#ifdef _WIN32
        // no POSIX UID concept on Windows
        return 0;
#else
        return getuid();
#endif
}

//   GPIO ON A HOST BUILD: there is no pin, so these record intent and nothing more. Present
// rather than omitted because host builds exist to exercise the portable logic ABOVE the
// platform layer -- an application that drives an activity LED should run here unchanged, and
// linking should not be what stops it.
//
//   the state is not modelled and there is no read side, deliberately: a fake that remembered
// levels would invite tests to assert on them, and asserting that a pin this platform does not
// have holds a value is a test of the fake rather than of the code under test.
void light_platform_gpio_configure_output(uint32_t pin, bool initial)
{
        light_debug("gpio: pin %u configured as output, initially %s",
                                        (unsigned) pin, initial ? "high" : "low");
}

void light_platform_gpio_write(uint32_t pin, bool value)
{
        light_trace("gpio: pin %u <- %s", (unsigned) pin, value ? "high" : "low");
}
