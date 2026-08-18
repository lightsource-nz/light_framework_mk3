#ifndef _LIGHT_BOARD_H
#define _LIGHT_BOARD_H

//   WeAct STM32F446 core board. Confirmed on hardware over SWD: device id 0x10006421,
// Cortex-M4 r0p1, 512 KiB flash -- OpenOCD's own STM32F4 database names this "STM32F446 - Rev:
// A". Its exact commercial name/silkscreen version was not identified (no matching WeAct
// public repo found); the pin assignments below were read directly off the board's own
// silkscreen rather than assumed from a similar-looking board.
//
//   CLOCKS. As with the F411 port, ST's SystemInit() does not configure the PLL on F4 -- it
// leaves the core on HSI at 16MHz until something configures the PLL, and
// light_platform_init() derives its SysTick reload from SystemCoreClockUpdate(), so the tick
// stays a millisecond either way. HSE_HZ is the common value for this class of F4 "core
// board" (many ship a 25MHz crystal) but is NOT confirmed on this specific unit -- verify
// before relying on it for anything that needs the PLL.
#define WEACT_STM32F446_HSE_HZ          25000000
#define WEACT_STM32F446_HSI_HZ          16000000

//   USER LED: silkscreened "B2" on this board, i.e. PB2. Polarity not yet confirmed on
// hardware (assumed active-low, matching every other WeAct board here) -- flip
// WEACT_STM32F446_LED_ACTIVE_HIGH if the demo blinks the LED backwards (visibly on, then a
// brief flash off, rather than the reverse).
#define WEACT_STM32F446_LED_PORT        GPIOB
#define WEACT_STM32F446_LED_PIN         2
#define WEACT_STM32F446_LED_ACTIVE_HIGH 0

//   Two user buttons, silkscreened "B0" and "C13" -- i.e. PB0 and PC13. Neither is wired up by
// the demo yet; named here since they are a physical fact about the board, not something to
// rediscover later.
#define WEACT_STM32F446_KEY0_PORT       GPIOB
#define WEACT_STM32F446_KEY0_PIN        0
#define WEACT_STM32F446_KEY1_PORT       GPIOC
#define WEACT_STM32F446_KEY1_PIN        13

// SWD, exposed on the header. There is no on-board debug probe: flashing needs an external
// ST-Link/CMSIS-DAP -- this board has been flashed and debugged through an ST-Link
#define WEACT_STM32F446_SWDIO_PORT      GPIOA
#define WEACT_STM32F446_SWDIO_PIN       13
#define WEACT_STM32F446_SWCLK_PORT      GPIOA
#define WEACT_STM32F446_SWCLK_PIN       14

#endif
