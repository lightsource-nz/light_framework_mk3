#include <light.h>
#if(LIGHT_SYSTEM != SYSTEM_CMSIS)
        #error "this file should only be compiled for a bare-CMSIS target"
#endif


#define LIGHT_PLATFORM_MAX_TIMERS       8
#define _INSTANCE_INVALID               16

#define TIMER_IDLE                      0
#define TIMER_RUN                       1

static struct lp_timer *timer_instance[LIGHT_PLATFORM_MAX_TIMERS];

light_task_t main_task;
static uint32_t system_time_at_init;

//   the millisecond tick. `volatile` because it is written by the SysTick handler and read by
// everything else; `uint32_t` because that is what the light_platform time API returns, and it
// wraps after ~49 days. every comparison below is written as a SUBTRACTION rather than as
// `a < b`, which is what makes the wrap harmless: unsigned subtraction of two times either
// side of the wrap still yields the correct interval.
static volatile uint32_t _tick_ms;

//   SysTick's own handler. named exactly this because that is the symbol ST's
// the chip port's startup file puts in the vector table -- it declares a weak default that spins
// forever, and this strong definition replaces it at link time. get the name wrong and there
// is no error: the weak stub stays, the tick never advances, and every timeout in the system
// waits forever.
void SysTick_Handler(void)
{
        _tick_ms++;
}

void light_platform_init()
{
        //   SystemCoreClock is maintained by CMSIS's system_stm32*.c, and reflects whatever
        // clock configuration is actually in force -- HSI at 16MHz out of reset, unless a board
        // hook has since reconfigured the PLL and called SystemCoreClockUpdate(). deriving the
        // reload from it rather than from a hardcoded frequency is what keeps the millisecond
        // tick a millisecond either way.
        //   SysTick_Config() also sets the lowest interrupt priority and enables the counter.
        SystemCoreClockUpdate();
        SysTick_Config(SystemCoreClock / 1000U);

        //   KEEP THE DEBUG INTERFACE ALIVE ACROSS __WFI(). Both light_platform_sleep_ms() and
        // light_core_port_condition_wait() idle in WFI, and by default the debug unit's clock
        // stops with the core -- so a running application becomes unreachable over SWD, with
        // openocd reporting "Cortex-M CPUID: 0x0 is unrecognized" as though the part were dead.
        // Connecting under reset still works, which is the tell: the silicon is fine, the debug
        // clock is simply off.
        //   Costs some idle current, which is the right trade on a development board and the
        // same thing ST's own tooling does. The bit names differ by family because the H7 gates
        // per power domain where the F4 has a single sleep bit.
#if defined(STM32H743xx)
        DBGMCU->CR |= DBGMCU_CR_DBG_SLEEPD1 | DBGMCU_CR_DBG_STOPD1 | DBGMCU_CR_DBG_STANDBYD1;
#elif defined(STM32F411xE)
        DBGMCU->CR |= DBGMCU_CR_DBG_SLEEP | DBGMCU_CR_DBG_STOP | DBGMCU_CR_DBG_STANDBY;
#endif

        //   before anything can want to log. light_core/src/stream.c prints during its own
        // setup, which runs after this, so a console brought up any later would silently drop
        // the framework's first few lines -- the ones that say what it found.
        //   the RP2 ports do the same thing with stdio_init_all() a few lines up from here
        light_core_port_console_init();

        // there is one core and no scheduler, so "main" is simply the only context there is
        main_task = 0;
        system_time_at_init = light_platform_get_absolute_time_ms();
}
light_task_t light_platform_get_task()
{
        return 0;
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
        if(!t)
                return NULL;
        if((t->id = _get_free_timer_instance()) != _INSTANCE_INVALID) {
                t->state = TIMER_IDLE;
                t->duration_ms = 0;
                t->target_ms = 0;
                t->timeout = NULL;
                timer_instance[t->id] = t;
                return t;
        }
        light_free(t);
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
                timer->target_ms = light_platform_get_absolute_time_ms() + timer->duration_ms;
                timer->state = TIMER_RUN;
        }
}
void light_platform_timer_stop(struct lp_timer *timer)
{
        if(timer->state == TIMER_RUN) {
                uint32_t now = light_platform_get_absolute_time_ms();
                // subtraction, not `target < now`: see the note on _tick_ms. the cast makes the
                // wrap-safe comparison explicit -- a target already in the past yields a
                // difference whose top bit is set
                int32_t remaining = (int32_t)(timer->target_ms - now);
                timer->duration_ms = (remaining > 0) ? (uint32_t)remaining : 0;
                timer->state = TIMER_IDLE;
        }
}
uint32_t light_platform_timer_get_remaining_ms(struct lp_timer *timer)
{
        switch(timer->state) {
        case TIMER_IDLE:
                return timer->duration_ms;
        case TIMER_RUN:;
                int32_t remaining = (int32_t)(timer->target_ms - light_platform_get_absolute_time_ms());
                return (remaining > 0) ? (uint32_t)remaining : 0;
        }
        return 0;
}
uint32_t light_platform_get_absolute_time_ms()
{
        return _tick_ms;
}
uint32_t light_platform_get_time_since_init()
{
        return light_platform_get_absolute_time_ms() - system_time_at_init;
}

void light_platform_sleep_ms(uint32_t period)
{
        uint32_t start = light_platform_get_absolute_time_ms();
        // __WFI() rather than a busy loop: SysTick fires every millisecond regardless, so the
        // core wakes often enough to make the deadline without spinning through it at full
        // current. any other interrupt also wakes it early, which the loop condition absorbs
        while((light_platform_get_absolute_time_ms() - start) < period) {
                __WFI();
        }
}
