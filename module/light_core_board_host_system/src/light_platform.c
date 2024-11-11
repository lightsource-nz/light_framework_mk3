/*
 *  light_core_board_host_system/src/light_platform.c
 *  provides access to low level platform facilities such as timers
 *  (host system version- uses POSIX APIs and aims for maximum compatibility)
 * 
 *  authored by Alex Fulton
 *  created november 2024
 */
#define _XOPEN_SOURCE 700
#include <light.h>

#include <unistd.h>
#ifndef _POSIX_TIMERS
        #error "light framework native application host requires POSIX timer support"
#endif
// include standard POSIX system headers
#include <signal.h>
#include <time.h>

#define LIGHT_PLATFORM_MAX_TIMERS       8
#define _INSTANCE_INVALID               16

#define TIMER_IDLE                      0
#define TIMER_RUN                       1

static struct lp_timer *timer_instance[LIGHT_PLATFORM_MAX_TIMERS];
static uint8_t timer_instance_count = 0;

void light_platform_init()
{

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
        struct sigevent event;
        struct sigaction action;
        timer_t timer_id;
        
        
        timer_create(CLOCK_MONOTONIC, &event, &timer_id);
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
                //timer->target_ms = to_ms_since_boot(delayed_by_ms(get_absolute_time(), timer->duration_ms));
                timer->state = TIMER_RUN;
        }
}
void light_platform_timer_stop(struct lp_timer *timer)
{
        if(timer->state == TIMER_RUN) {
        //        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        //        if(timer->target_ms <= now_ms) {
        //                timer->duration_ms = 0;
        //        } else {
        //                timer->duration_ms = timer->target_ms - now_ms;
        //        }
        }
}
uint32_t light_platform_timer_get_remaining_ms(struct lp_timer *timer)
{
        switch(timer->state) {
                case TIMER_IDLE:
                return timer->duration_ms;
                case TIMER_RUN:;
                uint32_t now = light_platform_get_system_time_ms();
                if(timer->target_ms < now) {
                        return timer->target_ms - now;
                }
                return 0;
        }
}
uint32_t light_platform_get_system_time_ms()
{
        struct timespec ts;
        if(clock_gettime(CLOCK_BOOTTIME, &ts) == -1) {
                return -1;
        }
        return (ts.tv_sec * 1000) + (ts.tv_nsec * 1000 * 1000);
}
