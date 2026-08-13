/*
 *  console.c
 *  USART console for the STM32H743 port
 *
 *  Same job and same shape as the F411 port's, and kept separate rather than shared because
 *  the USART peripheral is a different generation. Four things differ, and every one of them
 *  fails silently rather than loudly if carried over unchanged:
 *
 *    - status register is ISR, not SR
 *    - data register is TDR, not DR
 *    - the transmit-ready flag is USART_ISR_TXE_TXFNF, because this USART has FIFOs
 *    - GPIO clocks live on AHB4 here, not AHB1
 *
 *  Everything above the register level -- _write() as a strong symbol beating libnosys's stub,
 *  unbuffered stdio, \r before \n -- is identical, and the reasoning for it is in the F411
 *  port's console.c.
 */
#include <light.h>
#if(LIGHT_SYSTEM != SYSTEM_CMSIS)
        #error "this file should only be compiled for a bare-CMSIS target"
#endif

#include <stm32h7xx.h>
#include <stdio.h>

// USART1 on PA9/PA10, overridable for a board that routes its console elsewhere
#ifndef LIGHT_CONSOLE_BAUD
#define LIGHT_CONSOLE_BAUD              115200
#endif

void light_core_port_console_init(void)
{
        // AHB4, not AHB1: on H7 the GPIO ports sit in the D3 domain on AHB4. Enabling the
        // wrong bus writes a reserved bit and leaves the port unclocked, which reads back as
        // zero and ignores writes without any error
        RCC->AHB4ENR |= RCC_AHB4ENR_GPIOAEN;
        RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

        // PA9 (TX) and PA10 (RX) to alternate function 7, same as on F4
        GPIOA->MODER &= ~((3U << (9 * 2)) | (3U << (10 * 2)));
        GPIOA->MODER |= ((2U << (9 * 2)) | (2U << (10 * 2)));
        GPIOA->AFR[1] &= ~((0xFU << ((9 - 8) * 4)) | (0xFU << ((10 - 8) * 4)));
        GPIOA->AFR[1] |= ((7U << ((9 - 8) * 4)) | (7U << ((10 - 8) * 4)));
        GPIOA->OSPEEDR |= ((2U << (9 * 2)) | (2U << (10 * 2)));

        //   the USART kernel clock, not the CPU clock. They are the same number here only
        // because every prescaler is 1 out of reset (HSI at 64MHz straight through to pclk2)
        // and USART16SEL in RCC_D2CCIP2R defaults to rcc_pclk2. Configure the PLL or any
        // prescaler without revisiting this and the console turns to line noise -- which reads
        // as a wiring fault rather than as a clock change.
        //   Rounded rather than truncated, as on the F411 port
        USART1->BRR = (SystemCoreClock + (LIGHT_CONSOLE_BAUD / 2)) / LIGHT_CONSOLE_BAUD;
        USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
}

//   one character to both backends. ITM first because it is the one that costs nothing when
// unused: ITM_SendChar() checks ITMENA in ITM->TCR and the port-0 bit in ITM->TER, both of
// which only a debugger sets, so with nothing attached this compiles to a load, a test and a
// branch. The USART write blocks on TXE regardless of whether anything is listening, which is
// the right trade for a log line but means it is never free.
//   Two backends rather than a choice between them, because they fail in opposite ways: SWO
// needs a debugger attached and one wire that is already part of the SWD header, while the
// USART needs no debugger but a wire to something that may not be broken out. Whichever a
// given board and setup has, the log comes out.
static void _console_putc(uint8_t c)
{
        ITM_SendChar(c);

        // TXE_TXFNF, not TXE: with the FIFO present this flag means "transmit FIFO not full"
        // as well as "transmit register empty", and it is the only spelling the H7 headers
        // define
        while(!(USART1->ISR & USART_ISR_TXE_TXFNF)) { }
        USART1->TDR = c;
}

int _write(int fd, const char *buf, int len)
{
        if(fd != 1 && fd != 2)
                return -1;

        for(int i = 0; i < len; i++) {
                if(buf[i] == '\n')
                        _console_putc('\r');
                _console_putc((uint8_t)buf[i]);
        }
        return len;
}
