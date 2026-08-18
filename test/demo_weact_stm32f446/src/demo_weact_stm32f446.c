/*
 *  demo_weact_stm32f446.c
 *  minimal light_framework application for the WeAct STM32F446 core board
 *
 *  See demo_blackpill.c for why this exists at all: every light_framework module is an
 *  INTERFACE library, so this is what actually exercises the STM32F446 port.
 *
 *  Blinks the user LED (PB2, silkscreened "B2") once a second from the application's periodic
 *  main, exercising the SysTick tick, light_platform's millisecond clock and the framework run
 *  loop together.
 */
#include <light.h>
#include <light_platform.h>
#include <light_board.h>

#include <stm32f4xx.h>

static void demo_weact_stm32f446_event(const struct light_module *mod, uint8_t event, void *arg);
static uint8_t demo_weact_stm32f446_main(struct light_application *app);

Light_Application_Define(demo_weact_stm32f446, demo_weact_stm32f446_event,
                                                demo_weact_stm32f446_main, &light_core);

#define BLINK_INTERVAL_MS       500

static uint32_t next_toggle_ms;
static bool led_on;

static void _led_write(bool on)
{
        // BSRR's high half resets, low half sets -- one write, no read-modify-write, as on the
        // F411 port's demo. Polarity per light_board.h; see the note there on why it is not
        // yet confirmed on hardware
        if(on == WEACT_STM32F446_LED_ACTIVE_HIGH)
                WEACT_STM32F446_LED_PORT->BSRR = (1U << WEACT_STM32F446_LED_PIN);
        else
                WEACT_STM32F446_LED_PORT->BSRR = (1U << (WEACT_STM32F446_LED_PIN + 16));
}

static void demo_weact_stm32f446_event(const struct light_module *mod, uint8_t event, void *arg)
{
        switch(event) {
        case LF_EVENT_MODULE_LOAD:
                // GPIOB is on AHB1, same bus every F4 port here uses for GPIO clocks
                RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;

                // general-purpose output, push-pull, low speed, no pull -- same MODER/OTYPER/
                // PUPDR sequence as the F411 port's demo, since this chip's GPIO block is
                // identical to it
                WEACT_STM32F446_LED_PORT->MODER &= ~(3U << (WEACT_STM32F446_LED_PIN * 2));
                WEACT_STM32F446_LED_PORT->MODER |= (1U << (WEACT_STM32F446_LED_PIN * 2));
                WEACT_STM32F446_LED_PORT->OTYPER &= ~(1U << WEACT_STM32F446_LED_PIN);
                WEACT_STM32F446_LED_PORT->PUPDR &= ~(3U << (WEACT_STM32F446_LED_PIN * 2));

                _led_write(false);
                next_toggle_ms = light_platform_get_time_since_init() + BLINK_INTERVAL_MS;

                light_info("weact_stm32f446 demo up: core clock %u Hz", (unsigned)SystemCoreClock);
                break;
        case LF_EVENT_MODULE_UNLOAD:
                break;
        }
}

static uint8_t demo_weact_stm32f446_main(struct light_application *app)
{
        uint32_t now = light_platform_get_time_since_init();
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
