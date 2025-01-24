/*
 *  light_core_board_host_os/src/light_platform.c
 *  provides access to low level platform facilities such as timers
 *  (host system version- uses POSIX APIs and aims for maximum compatibility)
 * 
 *  TODO: this implementation should use a hierarchical timing wheel architecture
 * 
 *  authored by Alex Fulton
 *  created november 2024
 */
#ifdef __GNUC__
#define _GNU_SOURCE
#endif
#define _XOPEN_SOURCE 700
#include <light.h>

#include <unistd.h>
#ifndef _POSIX_TIMERS
        #error "light framework native application host requires POSIX timer support"
#endif
// include standard POSIX system headers
#include <signal.h>
#include <time.h>
#define LIGHT_PLATFORM_TIMER_SIGNAL     SIGRTMIN

#define LIGHT_PLATFORM_MAX_TIMERS       8
#define _INSTANCE_INVALID               16

#define TIMER_IDLE                      0
#define TIMER_RUN                       1

static struct lp_timer *timer_instance[LIGHT_PLATFORM_MAX_TIMERS];
static uint8_t timer_instance_count = 0;
static timer_t system_timer_id;

static uint8_t _get_free_timer_instance();
static void _light_platform_timer_signal_handler(int sig, siginfo_t *si, void *uc);
static uint32_t system_time_at_init;

void light_platform_init()
{
        struct sigevent event;
        struct sigaction action;
        int status;

        system_time_at_init = light_platform_get_absolute_time_ms();
        
        action.sa_flags = SA_SIGINFO;
        action.sa_sigaction = _light_platform_timer_signal_handler;

        sigemptyset(&action.sa_mask);
        if ((status = sigaction(LIGHT_PLATFORM_TIMER_SIGNAL, &action, NULL)) != 0){
                light_fatal("could not register system timer signal: sigaction() failed with return code [%d]", status);
        }
        
        if((status = timer_create(CLOCK_MONOTONIC, NULL, &system_timer_id)) != 0) {
                light_fatal("could not create system timer: timer_create() failed with return code [%d]", status);
        }
}
static uint8_t _get_free_timer_instance()
{
        for(uint8_t i = 0; i < LIGHT_PLATFORM_MAX_TIMERS; i++) {
                if(timer_instance[i] == NULL)
                        return i;
        }
        return _INSTANCE_INVALID;
}
static void _light_platform_update_timer(struct lp_timer *timer, uint32_t time);
static void _light_platform_timer_signal_handler(int sig, siginfo_t *si, void *uc)
{
        uint32_t time = light_platform_get_absolute_time_ms();
        for(uint8_t i = 0; i < timer_instance_count; i++) {
                if(timer_instance[i]->state) {
                        _light_platform_update_timer(timer_instance[i], time);
                }
        }
}
static void _light_platform_update_timer(struct lp_timer *timer, uint32_t time)
{
        if(timer->state) {
                uint32_t remaining = timer->target_ms - time;
                if(remaining > 0) {

                }
        }
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
                uint32_t now = light_platform_get_absolute_time_ms();
                if(timer->target_ms < now) {
                        return timer->target_ms - now;
                }
                return 0;
        }
}
uint32_t light_platform_get_absolute_time_ms()
{
        struct timespec ts;
        if(clock_gettime(CLOCK_BOOTTIME, &ts) == -1) {
                return -1;
        }
        return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000);
}
uint32_t light_platform_get_time_since_init()
{
        return light_platform_get_absolute_time_ms() - system_time_at_init;
}
static uint8_t *_do_getenv(uint8_t *name)
{
#ifdef __GNUC__
        return secure_getenv(name);
#else
        return getenv(name);
#endif
}
uint8_t *light_platform_get_user_home()
{
        return _do_getenv("HOME");
}
uint8_t *light_platform_get_user_name()
{
        return _do_getenv("USER");
}
uint16_t light_platform_get_user_id()
{
        return getuid();
}
