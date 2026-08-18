/*
 *  console.c
 *  USART console for the STM32F103 port
 *
 *  See the STM32F411 port's console.c for why this exists at all (light_core's stream layer
 *  reaches it purely through printf()/_write()). The USART register interface itself (SR, DR,
 *  BRR, CR1) is unchanged from the F4 -- what differs on the F1 is how the pins reach it: no
 *  MODER/AFR here, and the GPIO clock sits on APB2 rather than AHB1.
 */
#include <light.h>
#if(LIGHT_SYSTEM != SYSTEM_CMSIS)
        #error "this file should only be compiled for a bare-CMSIS target"
#endif

#include <stm32f1xx.h>
#include <stdio.h>

//   USART1 on PA9/PA10, the default (unremapped) mapping -- where an ST-Link's virtual COM
// port is normally wired on this class of board. Overridable per board, as on the F411 port.
#ifndef LIGHT_CONSOLE_BAUD
#define LIGHT_CONSOLE_BAUD              115200
#endif

void light_core_port_console_init(void)
{
        // GPIOA and USART1 both hang off APB2 on the F1 -- there is no separate AHB1 the way
        // the F4 has one. IOPAEN is bit 2, not bit 0: AFIOEN and two USB/CAN-adjacent bits sit
        // below it in APB2ENR
        RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_USART1EN;

        //   PA9 (TX): alternate-function push-pull, 50MHz (CNF=10, MODE=11).
        //   PA10 (RX): floating input (CNF=01, MODE=00) -- the F1 has no per-pin AFR selecting
        // WHICH peripheral takes a pin; the peripheral wanting an AF pin only asks for push-pull
        // AF mode, and which function actually lands there is fixed by silicon (or moved with
        // AFIO_MAPR's whole-peripheral remap bit, not used here since this is the default map).
        // Both pins are in CRH: bit offset (n-8)*4 for pin n in [8,15].
        GPIOA->CRH &= ~((0xFU << ((9 - 8) * 4)) | (0xFU << ((10 - 8) * 4)));
        GPIOA->CRH |= ((0xBU << ((9 - 8) * 4)) | (0x4U << ((10 - 8) * 4)));

        //   BRR from the clock that actually feeds USART1, rounded rather than truncated -- see
        // the F411 port's console.c for why. SystemCoreClock is PCLK2 here only because the
        // APB2 prescaler is 1 out of reset; a board that configures the PLL must divide by its
        // own APB2 prescaler, exactly as there
        USART1->BRR = (SystemCoreClock + (LIGHT_CONSOLE_BAUD / 2)) / LIGHT_CONSOLE_BAUD;
        USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

        //   unbuffered, both streams -- see the F411 port's console.c for why this matters on
        // a board that logs a few lines and then sits in its run loop
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
}

//   newlib's write syscall -- a strong symbol, taking precedence over libnosys's stub. See the
// F411 port's console.c for why this is blocking rather than buffered.
int _write(int fd, const char *buf, int len)
{
        if(fd != 1 && fd != 2)
                return -1;

        for(int i = 0; i < len; i++) {
                if(buf[i] == '\n') {
                        while(!(USART1->SR & USART_SR_TXE)) { }
                        USART1->DR = '\r';
                }
                while(!(USART1->SR & USART_SR_TXE)) { }
                USART1->DR = (uint8_t)buf[i];
        }
        return len;
}
