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
                uint32_t now = light_platform_get_time_since_init();
                if(timer->target_ms < now) {
                        return timer->target_ms - now;
                }
                return 0;
        }
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
