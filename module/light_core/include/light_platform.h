#ifndef _LIGHT_PLATFORM_H
#define _LIGHT_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>

#include <light_platform_port.h>

//   how light_fatal() ends the program once it has said why. exit() is right wherever there is
// a process to leave, and wrong on bare metal, where it halts a board that then cannot be
// reflashed: the 1200-baud reset that normally reboots it is served by the firmware's own USB
// stack, and a halted device no longer serves anything. Recovery then needs the physical BOOT
// button, which is no help on a board in a case or on someone else's desk.
//   ports that can do better define light_platform_abort() in their light_platform_port.h
#ifndef light_platform_abort
#define light_platform_abort() exit(-1)
#endif
// included here rather than left for consumers to find, so that anything already reaching for
// light_platform.h gets the PWM API without a second include
#include <light_platform_pwm.h>

struct lp_timer {
        uint8_t id;
        uint8_t state;
        uint32_t duration_ms;
        uint32_t target_ms;
        void (*timeout)(void *);
};

// TODO decide if we need to be able to link against this directly or not
extern light_task_t main_task;

extern void light_platform_init();
extern light_task_t light_platform_get_task();
extern light_task_t light_platform_get_main_task();
static inline bool light_platform_get_task_is_main(light_task_t task_id)
{
    return task_id == light_platform_get_main_task();
}
static inline bool light_platform_task_is_main()
{
        return light_platform_get_task_is_main(light_platform_get_task());
}
extern struct lp_timer *light_platform_timer_new();
extern uint8_t light_platform_timer_set_ms(struct lp_timer *timer, uint32_t time_ms, bool start);
extern void light_platform_timer_run(struct lp_timer *timer);
extern void light_platform_timer_stop(struct lp_timer *timer);
extern uint32_t light_platform_timer_get_remaining_ms(struct lp_timer *timer);
static inline bool light_platform_timer_get_expired(struct lp_timer *timer)
{
        return light_platform_timer_get_remaining_ms(timer) == 0;
}

extern uint32_t light_platform_get_absolute_time_ms();
extern uint32_t light_platform_get_time_since_init();
extern void light_platform_sleep_ms(uint32_t period);

extern uint8_t *light_platform_getenv(const uint8_t *name);
extern uint8_t *light_platform_get_user_home();
extern uint8_t *light_platform_get_user_name();
extern uint16_t light_platform_get_user_id();

#endif
