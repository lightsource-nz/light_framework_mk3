/*
 *  demo_mini_stm32h7.c
 *  minimal light_framework application for the WeAct MiniSTM32H7xx (STM32H743VIT6)
 *
 *  The counterpart of demo_blackpill, and there for the same reason: every light_framework
 *  module is an INTERFACE library, so without an executable a "successful build" for this
 *  board compiles nothing at all.
 *
 *  Blinks the user LED once a second from the application's periodic main, which exercises
 *  SysTick, light_platform's millisecond clock and the framework run loop together, and logs
 *  its core clock over the console.
 */
#include <light.h>
#include <light_platform.h>
#include <light_board.h>

#include <stm32h7xx.h>

static void demo_mini_stm32h7_event(const struct light_module *mod, uint8_t event, void *arg);
static uint8_t demo_mini_stm32h7_main(struct light_application *app);

Light_Application_Define(demo_mini_stm32h7, demo_mini_stm32h7_event, demo_mini_stm32h7_main,
                                                                &light_core);

#define BLINK_INTERVAL_MS       500

static uint32_t next_toggle_ms;
static bool led_on;

static void _led_write(bool on)
{
        // BSRR's high half resets and low half sets, so this is one write with no
        // read-modify-write for an interrupt to land in the middle of
        if(on == MINI_STM32H7_LED_ACTIVE_HIGH)
                MINI_STM32H7_LED_PORT->BSRR = (1U << MINI_STM32H7_LED_PIN);
        else
                MINI_STM32H7_LED_PORT->BSRR = (1U << (MINI_STM32H7_LED_PIN + 16));
}

static void demo_mini_stm32h7_event(const struct light_module *mod, uint8_t event, void *arg)
{
        switch(event) {
        case LF_EVENT_MODULE_LOAD:
                // AHB4, not AHB1: on H7 the GPIO ports are in the D3 domain on AHB4. Enabling
                // the wrong bus leaves the port unclocked, reading back as zero and ignoring
                // writes, with no error anywhere
                RCC->AHB4ENR |= RCC_AHB4ENR_GPIOEEN;

                // general-purpose output, push-pull, no pull. MODER is two bits per pin
                MINI_STM32H7_LED_PORT->MODER &= ~(3U << (MINI_STM32H7_LED_PIN * 2));
                MINI_STM32H7_LED_PORT->MODER |= (1U << (MINI_STM32H7_LED_PIN * 2));
                MINI_STM32H7_LED_PORT->OTYPER &= ~(1U << MINI_STM32H7_LED_PIN);
                MINI_STM32H7_LED_PORT->PUPDR &= ~(3U << (MINI_STM32H7_LED_PIN * 2));

                _led_write(false);
                next_toggle_ms = light_platform_get_time_since_init() + BLINK_INTERVAL_MS;

                light_info("mini_stm32h7 demo up: core clock %u Hz", (unsigned)SystemCoreClock);
                break;
        case LF_EVENT_MODULE_UNLOAD:
                break;
        }
}

static uint8_t demo_mini_stm32h7_main(struct light_application *app)
{
        uint32_t now = light_platform_get_time_since_init();
        // subtraction rather than `now >= next`, so the ~49-day wrap of the millisecond
        // counter does not stall the blink for half its range
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
