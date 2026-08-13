/*
 *  console.c
 *  USART console for the STM32F411 port
 *
 *  light_core's stream handlers emit through printf()/vprintf() (see msg_stdout() and
 *  friends in light_core/src/stream.c), and stream.c's own setup prints before any stream
 *  exists. So the whole logging path is served by implementing one newlib syscall -- _write()
 *  -- rather than by giving light_stream a bespoke backend. Anything in the tree that reaches
 *  for printf() gets the console for free, which is the same deal the RP2 ports get from
 *  pico-sdk's stdio.
 *
 *  Until this existed, --specs=nosys.specs supplied a _write() stub that returns -1, so every
 *  light_info() on this port was formatted in full and then discarded.
 */
#include <light.h>
#if(LIGHT_SYSTEM != SYSTEM_CMSIS)
        #error "this file should only be compiled for a bare-CMSIS target"
#endif

#include <stm32f4xx.h>
#include <stdio.h>

//   USART1 on PA9/PA10 by default: that is where an ST-Link's virtual COM port is normally
// wired on this class of board, and it is the only USART whose default pins are broken out on
// a Blackpill without disturbing anything else.
//   Overridable, because which USART reaches a connector is a BOARD fact and this file is
// chip-level. A board that routes its console elsewhere defines these rather than forking the
// port.
#ifndef LIGHT_CONSOLE_BAUD
#define LIGHT_CONSOLE_BAUD              115200
#endif

void light_core_port_console_init(void)
{
        // GPIOA and USART1 clocks. USART1 is on APB2, GPIOA on AHB1 -- a peripheral whose
        // clock is off reads back zero and ignores writes without complaint, which is the
        // usual reason a correct-looking init sequence does nothing at all
        RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
        RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

        // PA9 (TX) and PA10 (RX) to alternate function 7. MODER is two bits per pin (10 =
        // alternate function); AFR[1] covers pins 8-15, four bits each, hence the -8
        GPIOA->MODER &= ~((3U << (9 * 2)) | (3U << (10 * 2)));
        GPIOA->MODER |= ((2U << (9 * 2)) | (2U << (10 * 2)));
        GPIOA->AFR[1] &= ~((0xFU << ((9 - 8) * 4)) | (0xFU << ((10 - 8) * 4)));
        GPIOA->AFR[1] |= ((7U << ((9 - 8) * 4)) | (7U << ((10 - 8) * 4)));
        GPIOA->OSPEEDR |= ((2U << (9 * 2)) | (2U << (10 * 2)));

        //   BRR from the clock that actually feeds USART1, rounded rather than truncated: at
        // 16MHz/115200 the exact divisor is 138.9, and truncating to 138 is a 0.6% error in the
        // wrong direction on top of whatever the HSI itself is off by.
        //   SystemCoreClock is PCLK2 here only because the APB2 prescaler is 1 out of reset. A
        // board that configures the PLL must also divide this by its APB2 prescaler, or the
        // console turns to line noise the moment the clock tree changes -- which is exactly the
        // kind of failure that looks like a wiring fault
        USART1->BRR = (SystemCoreClock + (LIGHT_CONSOLE_BAUD / 2)) / LIGHT_CONSOLE_BAUD;
        USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

        //   unbuffered, both streams. newlib buffers stdout when it cannot tell it is a
        // terminal -- which it never can here -- so with the default buffering a log line only
        // appears once ~1KB has accumulated or something calls fflush(). On a board that logs a
        // few lines and then sits in its run loop, that reads as "the console does not work"
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
}

//   newlib's write syscall. Defined here as a strong symbol, which takes precedence over the
// stub libnosys provides: the linker only pulls an archive member in to satisfy a symbol that
// is still undefined, so this one wins simply by existing.
//   Blocking, and deliberately so. A log line that returns before it has been sent is a log
// line that is lost when the next fault resets the part, which is when it matters most.
int _write(int fd, const char *buf, int len)
{
        if(fd != 1 && fd != 2)
                return -1;

        for(int i = 0; i < len; i++) {
                // \r ahead of \n: the framework emits bare newlines, and a terminal that is not
                // in ONLCR mode renders those as a staircase rather than as lines
                if(buf[i] == '\n') {
                        while(!(USART1->SR & USART_SR_TXE)) { }
                        USART1->DR = '\r';
                }
                while(!(USART1->SR & USART_SR_TXE)) { }
                USART1->DR = (uint8_t)buf[i];
        }
        return len;
}
