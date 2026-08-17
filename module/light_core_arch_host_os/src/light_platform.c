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

#include <threads.h>

#include <unistd.h>

#ifdef _WIN32
#include <windows.h>
#include <time.h>
#else
#ifndef _POSIX_TIMERS
        #error "light framework native application host requires POSIX timer support"
#endif
// include standard POSIX system headers
#include <signal.h>
#include <time.h>
#define LIGHT_PLATFORM_TIMER_SIGNAL     SIGRTMIN
#endif

#define LIGHT_PLATFORM_MAX_TIMERS       8
#define _INSTANCE_INVALID               16

#define TIMER_IDLE                      0
#define TIMER_RUN                       1

static struct lp_timer *timer_instance[LIGHT_PLATFORM_MAX_TIMERS];
static uint8_t timer_instance_count = 0;
#ifndef _WIN32
static timer_t system_timer_id;
#endif

static uint8_t _get_free_timer_instance();
#ifndef _WIN32
static void _light_platform_timer_signal_handler(int sig, siginfo_t *si, void *uc);
#endif
static uint32_t system_time_at_init;

thrd_t main_task;

void light_platform_init()
{
        system_time_at_init = light_platform_get_absolute_time_ms();

        main_task = thrd_current();

#ifndef _WIN32
        struct sigevent event;
        struct sigaction action;
        int status;

        action.sa_flags = SA_SIGINFO;
        action.sa_sigaction = _light_platform_timer_signal_handler;

        sigemptyset(&action.sa_mask);
        if ((status = sigaction(LIGHT_PLATFORM_TIMER_SIGNAL, &action, NULL)) != 0){
                light_fatal("could not register system timer signal: sigaction() failed with return code [%d]", status);
        }

        if((status = timer_create(CLOCK_MONOTONIC, NULL, &system_timer_id)) != 0) {
                light_fatal("could not create system timer: timer_create() failed with return code [%d]", status);
        }
#endif
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
#ifndef _WIN32
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
#endif
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
                //   a SIGNED difference, not `target < now`. The comparison here was inverted:
                // it returned target - now on the branch where the target had already passed,
                // which on unsigned counters underflows to nearly UINT32_MAX -- a timer that
                // had just expired reported roughly 49 days remaining. Taking the difference
                // first and testing its sign is also what keeps this correct across the
                // millisecond counter's own wrap, which comparing the two absolute values is
                // not. Matches light_core_chip_stm32_common, which already did it this way
                int32_t remaining = (int32_t)(timer->target_ms - light_platform_get_absolute_time_ms());
                return (remaining > 0) ? (uint32_t)remaining : 0;
        }
        //   every other state has nothing pending. Falling off the end was undefined behaviour:
        // the switch covers two states and the enum has more
        return 0;
}
uint32_t light_platform_get_absolute_time_ms()
{
#ifdef _WIN32
        return (uint32_t) GetTickCount64();
#else
        struct timespec ts;
        if(clock_gettime(CLOCK_BOOTTIME, &ts) == -1) {
                return -1;
        }
        //   1 ms is 1e6 ns. This divided by 1e3, which yields MICROseconds, and then added them
        // to a milliseconds figure -- so the sub-second part ran 1000x fast and, worse, the whole
        // value went BACKWARDS at every wall-clock second boundary: at .960s it read
        // sec*1000 + 960000, and 40ms later it read (sec+1)*1000 + 0, a drop of ~959000.
        //   any code measuring an elapsed interval across that boundary saw negative time. It
        // surfaced as light_audio.poll_ends_tone_on_the_clock failing on CI while passing
        // locally, because whether a 40ms sleep straddles a second boundary is luck -- roughly a
        // 4% chance per run, which is exactly the sort of odds that reads as "flaky test".
        //   the cast is deliberate rather than incidental: tv_sec is uptime here (CLOCK_BOOTTIME),
        // so this wraps after ~49.7 days, matching the RP2 millisecond counter. Callers already
        // difference these as int32_t for that reason, which stays correct across the wrap.
        return (uint32_t)(((uint64_t)ts.tv_sec * 1000) + ((uint64_t)ts.tv_nsec / 1000000));
#endif
}
uint32_t light_platform_get_time_since_init()
{
        return light_platform_get_absolute_time_ms() - system_time_at_init;
}
void light_platform_sleep_ms(uint32_t period)
{
        usleep(period * 1000);
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
