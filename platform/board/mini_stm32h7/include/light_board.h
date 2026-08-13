#ifndef _LIGHT_BOARD_H
#define _LIGHT_BOARD_H

//   WeAct MiniSTM32H7xx, fitted with STM32H743VIT6.
//
//   IDENTIFIED FROM THE SILICON, not from the silkscreen: DBGMCU_IDCODE reads 0x20036450
// (device 0x450, rev V) and the flash size register reads 2048K. The H750VB is the same die
// with 128K enabled, so the 2MB reading is what rules it out -- worth repeating on any board
// from this family, since WeAct ship both.
//   Unique ID of the unit this was read from: 0023003e 3133510f 38383232.
//
//   CLOCKS. ST's SystemInit() does not configure the PLL on H7 either, so the part runs on HSI
// at 64MHz until something does, with every prescaler at 1. light_platform_init() derives its
// SysTick reload from SystemCoreClockUpdate() rather than a constant, so the millisecond tick
// holds at either speed. 64MHz is the bring-up default: correct, and it needs no external
// crystal to be present or working.
//   The board fits a 25MHz HSE crystal.
#define MINI_STM32H7_HSE_HZ             25000000
#define MINI_STM32H7_HSI_HZ             64000000

//   USER LED: PE3, ACTIVE LOW. Confirmed on hardware -- GPIOE->ODR bit 3 was observed
// toggling in step with the demo's state over SWD, and the LED was then confirmed blinking by
// eye. Writing 1 turns it OFF.
#define MINI_STM32H7_LED_PORT           GPIOE
#define MINI_STM32H7_LED_PIN            3
#define MINI_STM32H7_LED_ACTIVE_HIGH    0

// USER BUTTON (marked KEY): PC13, active low. UNCONFIRMED -- unlike the LED above, nothing has
// exercised this yet.
#define MINI_STM32H7_KEY_PORT           GPIOC
#define MINI_STM32H7_KEY_PIN            13
#define MINI_STM32H7_KEY_ACTIVE_HIGH    0

// SWD on the 4-pin header. No on-board debug probe -- flashing needs an external ST-Link or
// CMSIS-DAP, or the ROM bootloader over USB with BOOT0 held
#define MINI_STM32H7_SWDIO_PORT         GPIOA
#define MINI_STM32H7_SWDIO_PIN          13
#define MINI_STM32H7_SWCLK_PORT         GPIOA
#define MINI_STM32H7_SWCLK_PIN          14

#endif
