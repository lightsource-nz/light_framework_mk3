#include <light.h>
#if(LIGHT_SYSTEM != SYSTEM_PICO_SDK)
        #error "this file should only be compiled when Pico SDK support is enabled"
#endif

#include <pico/time.h>
#include <pico/stdio.h>

#if LIGHT_PLATFORM_USB_ON_CORE1
#include <pico/multicore.h>
#include <tusb.h>
// set by the core 1 worker once tusb_init() has run there, so core 0 does not reach
// stdio_init_all() before there is anything pumping enumeration
static volatile bool _usb_core1_ready = false;
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
static void _core1_usb_worker(void)
{
        tusb_init();
        _usb_core1_ready = true;

        while(!light_core_port_worker_stop_requested()) {
                tud_task();
                //   the queues do not exist until light_stream_setup() runs on core 0, which
                // happens after this core is already pumping. Until then this loop exists
                // purely to carry enumeration through stdio_init_all()'s connect wait
                if(light_stream_drain_ready)
                        light_stream_service_message_queues();
        }
        light_core_port_worker_signal_finished();
}
#endif

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
