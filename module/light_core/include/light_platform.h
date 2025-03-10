#ifndef _LIGHT_PLATFORM_H
#define _LIGHT_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>

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
