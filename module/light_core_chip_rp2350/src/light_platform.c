#include <light.h>
#if(LIGHT_SYSTEM != SYSTEM_PICO_SDK)
        #error "this file should only be compiled when Pico SDK support is enabled"
#endif

#include <pico/time.h>
#include <pico/stdio.h>
#include <pico/bootrom.h>

//   1 by default: see light_core_port_abort() for why a halted board is the worse failure.
// Set to 0 when a debugger is attached and the halt is the point.
//   deliberately OUTSIDE the USB-on-core-1 guard below: an unflashable board is no less
// unflashable for having kept its USB stack on core 0, so both the abort path and the panic
// path want this whatever that setting is
#ifndef LIGHT_PANIC_ENTER_BOOTSEL
#define LIGHT_PANIC_ENTER_BOOTSEL 1
#endif

#if LIGHT_PLATFORM_USB_ON_CORE1
#include <pico/multicore.h>
#include <tusb.h>
#include <stdarg.h>
#include <stdio.h>
// set by the core 1 worker once tusb_init() has run there, so core 0 does not reach
// stdio_init_all() before there is anything pumping enumeration
static volatile bool _usb_core1_ready = false;

//   panic hand-off. core 0 owns no part of the USB stack here, so it cannot print its own
// dying words -- but it CAN format them, since vsnprintf() touches nothing but memory. So the
// message is rendered on the panicking core into this buffer and the printing handed to the
// core that owns the console.
//
//   worth doing rather than tolerating one last cross-core write, because a panic message is
// the one piece of output whose loss costs the most: it is the only evidence of why the board
// died, and the collision it would risk inside TinyUSB is exactly the kind that garbles or
// hangs the transfer that was carrying it
#ifndef LIGHT_PANIC_MESSAGE_MAX
#define LIGHT_PANIC_MESSAGE_MAX 256
#endif
// how long the panicking core waits for the other one to finish printing before halting
// anyway. A dead or wedged core 1 must not turn a panic into a silent hang
#define LIGHT_PANIC_HANDOFF_TIMEOUT_MS 2000
static char _panic_message[LIGHT_PANIC_MESSAGE_MAX];
static volatile bool _panic_pending = false;
static volatile bool _panic_printed = false;
#endif

#define LIGHT_PLATFORM_MAX_TIMERS       8
#define _INSTANCE_INVALID               16

#define TIMER_IDLE                      0
#define TIMER_RUN                       1

static struct lp_timer *timer_instance[LIGHT_PLATFORM_MAX_TIMERS];
static uint8_t timer_instance_count = 0;

light_task_t main_task;
static uint32_t system_time_at_init;

#if LIGHT_PLATFORM_USB_ON_CORE1
//   everything that touches TinyUSB lives on this core, and nothing else may touch it from
// anywhere. tud_task() here, tusb_init() here (dcd_int_enable() enables USBCTRL_IRQ on the
// CALLING core, and IRQ enables are per-core on RP2), and the message queue drained here too
// so that stdio_usb_out_chars() -- which runs tud_cdc_write()/tud_task() on whichever core
// called it -- is only ever entered from this one.
//
//   that co-location IS the fix. The board previously set HAS_MULTICORE_WORKER=0 because a
// core 1 stream worker raced TinyUSB running on core 0; putting both on core 1 removes the
// race rather than avoiding it, and gets the 500 ms-per-write CDC busy-wait off the task loop
// that renders the UI and reads the touch panel.
//
//   the cost is a rule the whole image has to keep: core 0 must never write to stdio again,
// because TinyUSB guards its event queue with IRQ-disable critical sections, which are
// per-core on RP2 and therefore not cross-core safe. One stray printf() on core 0 restores
// the original race in its most intermittent form
//   prints a message handed over by the other core, then keeps pumping long enough for it to
// actually leave the device. tud_task() is what moves bytes, so returning the instant printf()
// does would abandon the message in the FIFO -- the precise failure this whole mechanism
// exists to avoid
static void _core1_drain_panic(void)
{
        printf("\n*** PANIC (core 0) ***\n%s\n", _panic_message);
        for(uint32_t i = 0; i < 1000; i++) {
                tud_task();
                sleep_ms(1);
        }
        _panic_printed = true;
}

static void _core1_usb_worker(void)
{
        tusb_init();
        _usb_core1_ready = true;

        while(!light_core_port_worker_stop_requested()) {
                tud_task();
                if(_panic_pending) {
                        _core1_drain_panic();
                        // the system is over; stay here pumping so a host connecting late
                        // still gets the message, rather than returning into a dead runtime
                        while(true)
                                tud_task();
                }
                //   the queues do not exist until light_stream_setup() runs on core 0, which
                // happens after this core is already pumping. Until then this loop exists
                // purely to carry enumeration through stdio_init_all()'s connect wait
                if(light_stream_drain_ready)
                        light_stream_service_message_queues();
        }
        light_core_port_worker_signal_finished();
}
#endif

#if LIGHT_PLATFORM_USB_ON_CORE1
//   installed as PICO_PANIC_FUNCTION, so this catches the SDK's own panics -- failed
// assertions, hard faults, spinlock misuse -- and not merely calls we wrote ourselves. Those
// are the ones most likely to happen on core 0 and most worth reading.
void __attribute__((noreturn)) light_core_port_panic(const char *fmt, ...)
{
        //   formatting is safe on any core: vsnprintf() writes to memory and nothing else.
        // Only the WRITE has to happen on the core that owns TinyUSB
        va_list args;
        va_start(args, fmt);
        vsnprintf(_panic_message, sizeof(_panic_message), fmt ? fmt : "(no message)", args);
        va_end(args);

        //   two cases print directly, and both are correct rather than compromises: core 1
        // panicking IS the core that owns the console, and a panic before the worker exists
        // means nothing else is touching USB yet. Handing off in either case would wait on
        // something that is never going to answer
        if(get_core_num() == 1 || !_usb_core1_ready) {
                printf("\n*** PANIC (core %u) ***\n%s\n", (unsigned)get_core_num(),
                        _panic_message);
                stdio_flush();
        } else {
                _panic_pending = true;
                //   bounded, because the alternative failure is a board that dies in total
                // silence. If core 1 cannot answer -- wedged, or panicking itself -- we lose
                // the message, but we do not also lose the breakpoint that tells a debugger
                // something went wrong
                for(uint32_t i = 0; i < LIGHT_PANIC_HANDOFF_TIMEOUT_MS && !_panic_printed; i++)
                        sleep_ms(1);
        }

        //   into BOOTSEL rather than halting. A halted board is an UNFLASHABLE board: the
        // 1200-baud reset that normally reboots it is served by the firmware's own USB stack,
        // and once a panic has taken the device down that route is gone -- recovery then needs
        // physical access to the BOOT button, which is no help on a board in a case, on a
        // bench across the room, or in the hands of someone who did not build it.
        //   the message has already been printed and pumped for a full second by this point,
        // so what is lost by rebooting is only the halted state itself. That state is worth
        // something to a debugger and nothing at all without one, which is why this is the
        // default and the halt is what has to be asked for
#if LIGHT_PANIC_ENTER_BOOTSEL
        reset_usb_boot(0, 0);
#endif
        __breakpoint();
        while(true)
                tight_loop_contents();
}
#endif

//   how light_fatal() ends things on this chip. The message has already been queued and
// flushed by the time this runs, so nothing is lost by rebooting -- and what is gained is a
// board that can still be flashed. exit() here halts the chip, and a halted chip no longer
// serves the USB stack that the 1200-baud reset depends on, so the only way back is the
// physical BOOT button. Learned the hard way: a deliberate test fatal left the board needing
// a hand on the hardware, twice
void light_core_port_abort(void)
{
#if LIGHT_PANIC_ENTER_BOOTSEL
        reset_usb_boot(0, 0);
#endif
        __breakpoint();
        while(true)
                tight_loop_contents();
}

void light_platform_init()
{
#if LIGHT_PLATFORM_USB_ON_CORE1
        //   before stdio_init_all(), not after. With PICO_STDIO_USB_ENABLE_IRQ_BACKGROUND_TASK
        // disabled, nothing in the SDK pumps tud_task() -- and stdio_usb_init()'s connect wait
        // (PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS) only sleeps and re-checks, so enumeration
        // would never complete and the board would hang at boot with no output to say why
        light_core_port_worker_launch(_core1_usb_worker);
        while(!_usb_core1_ready)
                tight_loop_contents();
#endif
        // apps that don't separately call tinyusb's board_init() (which does its own
        // stdio setup as a side effect -- crossfire relies on this) would otherwise never
        // get GPIO/UART configured for stdio at all, silently dropping every light_info()/
        // light_debug() call
        stdio_init_all();
        alarm_pool_init_default();
        // core 0 is always the entry point on RP2350, so it's simply hardcoded as "main"
        // here rather than tracked via any actual task/thread handle (there is no OS
        // scheduler). true of both of this chip's architectures -- the bootrom starts core 0
        // whether the image targets the Cortex-M33 pair or the Hazard3 RISC-V pair
        main_task = 0;
        system_time_at_init = light_platform_get_absolute_time_ms();
}
light_task_t light_platform_get_task()
{
        return get_core_num();
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
