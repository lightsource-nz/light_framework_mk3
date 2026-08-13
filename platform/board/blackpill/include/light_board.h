#ifndef _LIGHT_BOARD_H
#define _LIGHT_BOARD_H

//   WeAct STM32F411CEU6 "Blackpill" (v3.x).
//
//   CLOCKS. ST's SystemInit() does NOT configure the PLL on F4 -- it sets up FPU access and the
// vector table offset and returns, leaving the core on HSI at 16MHz. So this board runs at
// 16MHz until something configures the PLL, and light_platform_init() derives its SysTick
// reload from SystemCoreClockUpdate(), which reads the RCC registers rather than assuming.
// The tick stays a millisecond at either speed; only the clock rate changes. 16MHz is the
// deliberate bring-up default -- correct, and it needs no crystal to be present or working.
//   The board does fit a 25MHz crystal, which is also cmsis_device_f4's default HSE_VALUE, so
// nothing needs overriding to switch to HSE later.
#define BLACKPILL_HSE_HZ                25000000
#define BLACKPILL_HSI_HZ                16000000

//   USER LED: PC13, ACTIVE LOW. Writing 1 turns it OFF. This is the single most commonly
// mis-driven thing on this board, and the failure looks like "the LED is stuck on" rather than
// like a bug.
#define BLACKPILL_LED_PORT              GPIOC
#define BLACKPILL_LED_PIN               13
#define BLACKPILL_LED_ACTIVE_HIGH       0

//   USER BUTTON (marked KEY): PA0, ACTIVE LOW, with an external pull-up fitted, so no internal
// pull is required. NOT debounced in hardware.
#define BLACKPILL_KEY_PORT              GPIOA
#define BLACKPILL_KEY_PIN               0
#define BLACKPILL_KEY_ACTIVE_HIGH       0

// SWD, exposed on the 4-pin header. There is no on-board debug probe: flashing needs an
// external ST-Link/CMSIS-DAP, or the ROM bootloader over USB with BOOT0 held
#define BLACKPILL_SWDIO_PORT            GPIOA
#define BLACKPILL_SWDIO_PIN             13
#define BLACKPILL_SWCLK_PORT            GPIOA
#define BLACKPILL_SWCLK_PIN             14

#endif
