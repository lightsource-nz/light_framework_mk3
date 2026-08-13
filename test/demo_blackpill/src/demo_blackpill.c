/*
 *  demo_blackpill.c
 *  minimal light_framework application for the WeAct STM32F411CEU6 "Blackpill"
 *
 *  Exists to prove the bare-CMSIS port end to end: that light_core, light_platform, ST's
 *  startup and system_*.c and the linker script all compile and link together into something
 *  that runs, and that the framework's static-registration sections are actually populated.
 *  Everything else in light_framework is an INTERFACE library, so without an executable a
 *  "successful build" for this board compiles precisely nothing.
 *
 *  Blinks the user LED once a second from the application's periodic main, which exercises the
 *  SysTick tick, light_platform's millisecond clock and the framework run loop at once -- if
 *  the LED blinks at the right rate, all three are working.
 */
#include <light.h>
#include <light_platform.h>
#include <light_board.h>

#include <stm32f4xx.h>

static void demo_blackpill_event(const struct light_module *mod, uint8_t event, void *arg);
static uint8_t demo_blackpill_main(struct light_application *app);

Light_Application_Define(demo_blackpill, demo_blackpill_event, demo_blackpill_main,
                                                                &light_core);

#define BLINK_INTERVAL_MS       500

static uint32_t next_toggle_ms;
static bool led_on;

static void _led_write(bool on)
{
        // PC13 is ACTIVE LOW on this board (see light_board.h): the LED sits between 3V3 and
        // the pin, so driving the pin low is what lights it. BSRR's high half resets, low half
        // sets -- one write, no read-modify-write, so an interrupt cannot land in the middle
        if(on == BLACKPILL_LED_ACTIVE_HIGH)
                BLACKPILL_LED_PORT->BSRR = (1U << BLACKPILL_LED_PIN);
        else
                BLACKPILL_LED_PORT->BSRR = (1U << (BLACKPILL_LED_PIN + 16));
}

static void demo_blackpill_event(const struct light_module *mod, uint8_t event, void *arg)
{
        switch(event) {
        case LF_EVENT_MODULE_LOAD:
                // GPIOC off the AHB1 bus. a peripheral whose clock is not enabled reads back as
                // zero and ignores writes, silently -- this is the single most common reason a
                // correct-looking GPIO sequence does nothing at all on STM32
                RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;

                // general-purpose output, push-pull, low speed, no pull. MODER is two bits per
                // pin: clear both, then set 01 for output
                BLACKPILL_LED_PORT->MODER &= ~(3U << (BLACKPILL_LED_PIN * 2));
                BLACKPILL_LED_PORT->MODER |= (1U << (BLACKPILL_LED_PIN * 2));
                BLACKPILL_LED_PORT->OTYPER &= ~(1U << BLACKPILL_LED_PIN);
                BLACKPILL_LED_PORT->PUPDR &= ~(3U << (BLACKPILL_LED_PIN * 2));

                _led_write(false);
                next_toggle_ms = light_platform_get_time_since_init() + BLINK_INTERVAL_MS;

                light_info("blackpill demo up: core clock %u Hz", (unsigned)SystemCoreClock);
                break;
        case LF_EVENT_MODULE_UNLOAD:
                break;
        }
}

static uint8_t demo_blackpill_main(struct light_application *app)
{
        uint32_t now = light_platform_get_time_since_init();
        // subtraction rather than `now >= next`, so the ~49-day wrap of the millisecond counter
        // does not stall the blink for the second half of its range
        if((int32_t)(now - next_toggle_ms) >= 0) {
                led_on = !led_on;
                _led_write(led_on);
                next_toggle_ms = now + BLINK_INTERVAL_MS;
        }
        return LF_STATUS_RUN;
}

int main(void)
{
        light_framework_init();
        light_framework_run(0, NULL);
        return 0;
}
