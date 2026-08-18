/*
 *  demo_bluepill_plus.c
 *  minimal light_framework application for the WeAct BluePill+ (STM32F103C8T6)
 *
 *  See demo_blackpill.c for why this exists at all: it is what actually exercises the
 *  STM32F103 port, since every light_framework module is an INTERFACE library and a build
 *  with no executable compiles nothing.
 *
 *  Blinks the user LED once a second from the application's periodic main, exercising the
 *  SysTick tick, light_platform's millisecond clock and the framework run loop together.
 */
#include <light.h>
#include <light_platform.h>
#include <light_board.h>

#include <stm32f1xx.h>

static void demo_bluepill_plus_event(const struct light_module *mod, uint8_t event, void *arg);
static uint8_t demo_bluepill_plus_main(struct light_application *app);

Light_Application_Define(demo_bluepill_plus, demo_bluepill_plus_event, demo_bluepill_plus_main,
                                                                &light_core);

#define BLINK_INTERVAL_MS       500

static uint32_t next_toggle_ms;
static bool led_on;

static void _led_write(bool on)
{
        // PC13 is ACTIVE LOW on this board (see light_board.h): the LED sits between 3V3 and
        // the pin, so driving the pin low is what lights it. BSRR's high half resets, low half
        // sets -- one write, no read-modify-write, unchanged from the F4 port's demo
        if(on == BLUEPILL_PLUS_LED_ACTIVE_HIGH)
                BLUEPILL_PLUS_LED_PORT->BSRR = (1U << BLUEPILL_PLUS_LED_PIN);
        else
                BLUEPILL_PLUS_LED_PORT->BSRR = (1U << (BLUEPILL_PLUS_LED_PIN + 16));
}

static void demo_bluepill_plus_event(const struct light_module *mod, uint8_t event, void *arg)
{
        switch(event) {
        case LF_EVENT_MODULE_LOAD:
                // GPIOC is on APB2 here, not AHB1 -- the F1's GPIO clocks all sit on APB2 (see
                // light_core_chip_stm32_common's light_platform_gpio.c). IOPCEN is bit 4
                RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;

                //   PC13 is in CRH (pins 8-15): 4 config bits per pin, CNF[1:0] MODE[1:0].
                // General-purpose push-pull output at 2MHz is CNF=00, MODE=10 -- there is no
                // MODER/OTYPER/PUPDR on this chip the way there is on the F411's
                GPIOC->CRH &= ~(0xFU << ((13 - 8) * 4));
                GPIOC->CRH |= (0x2U << ((13 - 8) * 4));

                _led_write(false);
                next_toggle_ms = light_platform_get_time_since_init() + BLINK_INTERVAL_MS;

                light_info("bluepill_plus demo up: core clock %u Hz", (unsigned)SystemCoreClock);
                break;
        case LF_EVENT_MODULE_UNLOAD:
                break;
        }
}

static uint8_t demo_bluepill_plus_main(struct light_application *app)
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
