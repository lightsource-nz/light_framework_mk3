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
#elif defined(STM32F103xB)
#include <stm32f1xx.h>
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
// domain, AHB1 on the F4, and APB2 on the F1 -- where the port bit is also offset by 2
// (IOPAEN is bit 2, not bit 0), because AFIOEN and the two USB/CAN-adjacent bits sit below it.
// A port whose clock is off reads back as zero and ignores writes with no error at all, which
// presents as a wiring fault rather than a software one.
static void _port_clock_enable(uint32_t pin)
{
        uint32_t bit = 1U << PIN_PORT_INDEX(pin);
#if defined(STM32H743xx)
        RCC->AHB4ENR |= bit;
        (void) RCC->AHB4ENR;
#elif defined(STM32F103xB)
        RCC->APB2ENR |= (bit << 2);
        (void) RCC->APB2ENR;
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

#if defined(STM32F103xB)
        //   the F1's GPIO block predates MODER/OTYPER/PUPDR entirely: each pin gets 4 bits
        // (CNF[1:0] MODE[1:0]) in CRL (pins 0-7) or CRH (pins 8-15). CNF=00, MODE=10 is
        // general-purpose push-pull output at 2MHz -- plenty for an LED or a UART pin, and the
        // slowest setting is also the one least likely to ring on a jumper-wire prototype board.
        volatile uint32_t *cr = (n < 8) ? &port->CRL : &port->CRH;
        uint32_t shift = (n % 8) * 4;
        *cr &= ~(0xFU << shift);
        *cr |= (0x2U << shift);
#else
        port->OTYPER &= ~(1U << n);
        port->PUPDR &= ~(3U << (n * 2));
        port->MODER &= ~(3U << (n * 2));
        port->MODER |= (1U << (n * 2));        // 0b01 = general purpose output
#endif
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
