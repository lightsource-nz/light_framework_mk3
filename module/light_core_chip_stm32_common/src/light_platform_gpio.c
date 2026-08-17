/*
 *  light_platform_gpio.c -- discrete output pins on STM32.
 *
 *  In stm32_common rather than per chip: the GPIO block is identical across the families this
 *  framework ports to, and only the bus its clock sits on differs (handled below).
 *
 *  PIN ENCODING is the packed (port, pin) byte light_ioport defines -- high nibble the port
 *  index, low nibble the pin. Write it with LIGHT_IOPORT_PIN_STM32() rather than as a constant:
 *  a wrong nibble drives an unrelated pin rather than failing.
 */
#include <light.h>
#include <light_platform.h>

#if defined(STM32H743xx)
#include <stm32h7xx.h>
#else
#include <stm32f4xx.h>
#endif

#define PIN_PORT_INDEX(p)       (((p) >> 4) & 0xF)
#define PIN_NUMBER(p)           ((p) & 0xF)

static GPIO_TypeDef *_port_of(uint32_t pin)
{
        switch(PIN_PORT_INDEX(pin)) {
        case 0: return GPIOA;
        case 1: return GPIOB;
        case 2: return GPIOC;
        case 3: return GPIOD;
        case 4: return GPIOE;
        //   guarded individually, not from I upwards: a 48-pin F411 has GPIOA-E and GPIOH only,
        // so naming GPIOF unconditionally fails to compile there rather than degrading
#ifdef GPIOF
        case 5: return GPIOF;
#endif
#ifdef GPIOG
        case 6: return GPIOG;
#endif
#ifdef GPIOH
        case 7: return GPIOH;
#endif
#ifdef GPIOI
        case 8: return GPIOI;
#endif
        default: return NULL;
        }
}

//   GPIO clocks sit on different buses by family: AHB4 on the H7, where the ports live in the D3
// domain, and AHB1 on the F4. A port whose clock is off reads back as zero and ignores writes
// with no error at all, which presents as a wiring fault rather than a software one.
static void _port_clock_enable(uint32_t pin)
{
        uint32_t bit = 1U << PIN_PORT_INDEX(pin);
#if defined(STM32H743xx)
        RCC->AHB4ENR |= bit;
        (void) RCC->AHB4ENR;
#else
        RCC->AHB1ENR |= bit;
        (void) RCC->AHB1ENR;
#endif
}

void light_platform_gpio_configure_output(uint32_t pin, bool initial)
{
        GPIO_TypeDef *port = _port_of(pin);
        uint32_t n = PIN_NUMBER(pin);
        if(!port) {
                light_warn("gpio: pin 0x%x names no port on this part", (unsigned) pin);
                return;
        }
        _port_clock_enable(pin);

        //   the level goes down BEFORE the pin becomes an output. Reversed, the pin drives its
        // reset state for the instant between the two writes -- a visible flash on an LED, and a
        // real glitch on anything reading the line as an enable.
        light_platform_gpio_write(pin, initial);

        port->OTYPER &= ~(1U << n);
        port->PUPDR &= ~(3U << (n * 2));
        port->MODER &= ~(3U << (n * 2));
        port->MODER |= (1U << (n * 2));        // 0b01 = general purpose output
}

void light_platform_gpio_write(uint32_t pin, bool value)
{
        GPIO_TypeDef *port = _port_of(pin);
        uint32_t n = PIN_NUMBER(pin);
        if(!port)
                return;
        //   BSRR rather than a read-modify-write of ODR: a single atomic store, so an interrupt
        // touching another pin on the same port cannot lose this write. Setting is the low half,
        // clearing the high half.
        port->BSRR = value ? (1U << n) : (1U << (n + 16));
}
