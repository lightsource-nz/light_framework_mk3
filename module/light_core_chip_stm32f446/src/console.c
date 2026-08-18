/*
 *  console.c
 *  USART console for the STM32F446 port
 *
 *  Identical register interface to the F411 port's console.c (same GPIO/USART peripheral
 *  generation within the F4 family) -- see that file for the full rationale. Kept as its own
 *  copy rather than shared, matching how each F4x1/F4xx chip port here owns its console.c,
 *  since a board on this chip could route the console to a different USART entirely.
 */
#include <light.h>
#if(LIGHT_SYSTEM != SYSTEM_CMSIS)
        #error "this file should only be compiled for a bare-CMSIS target"
#endif

#include <stm32f4xx.h>
#include <stdio.h>

//   USART1 on PA9/PA10 by default -- see the F411 port's console.c for why. Overridable per
// board, as there.
#ifndef LIGHT_CONSOLE_BAUD
#define LIGHT_CONSOLE_BAUD              115200
#endif

void light_core_port_console_init(void)
{
        RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
        RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

        GPIOA->MODER &= ~((3U << (9 * 2)) | (3U << (10 * 2)));
        GPIOA->MODER |= ((2U << (9 * 2)) | (2U << (10 * 2)));
        GPIOA->AFR[1] &= ~((0xFU << ((9 - 8) * 4)) | (0xFU << ((10 - 8) * 4)));
        GPIOA->AFR[1] |= ((7U << ((9 - 8) * 4)) | (7U << ((10 - 8) * 4)));
        GPIOA->OSPEEDR |= ((2U << (9 * 2)) | (2U << (10 * 2)));

        USART1->BRR = (SystemCoreClock + (LIGHT_CONSOLE_BAUD / 2)) / LIGHT_CONSOLE_BAUD;
        USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
}

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
