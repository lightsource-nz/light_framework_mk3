#ifndef _LIGHT_BOARD_H
#define _LIGHT_BOARD_H

//   WeAct BluePill+ (STM32F103C8T6). Confirmed on hardware over SWD: device id 0x20036410,
// 64 KiB flash, Cortex-M3 r1p1 -- i.e. the same silicon "medium-density" group as the classic
// BluePill, just on WeAct's own board layout.
//
//   CLOCKS. ST's SystemInit() does NOT configure the PLL on F1 -- it leaves the core on HSI at
// 8MHz until something configures the PLL, exactly as the F411 port runs on its own HSI until
// configured. light_platform_init() derives its SysTick reload from SystemCoreClockUpdate(),
// so the tick stays a millisecond either way; only the clock rate changes. This board fits an
// 8MHz crystal, which is also cmsis_device_f1's default HSE_VALUE.
#define BLUEPILL_PLUS_HSE_HZ            8000000
#define BLUEPILL_PLUS_HSI_HZ            8000000

//   USER LED: PC13, ACTIVE LOW -- the same pin and polarity as the Blackpill's, which is not a
// coincidence: WeAct reuses this LED/pin convention across most of its small STM32 boards.
// Writing 1 turns it OFF.
#define BLUEPILL_PLUS_LED_PORT          GPIOC
#define BLUEPILL_PLUS_LED_PIN           13
#define BLUEPILL_PLUS_LED_ACTIVE_HIGH   0

// SWD, exposed on the 4-pin header. There is no on-board debug probe: flashing needs an
// external ST-Link/CMSIS-DAP -- this board has been flashed and debugged through an ST-Link
#define BLUEPILL_PLUS_SWDIO_PORT        GPIOA
#define BLUEPILL_PLUS_SWDIO_PIN         13
#define BLUEPILL_PLUS_SWCLK_PORT        GPIOA
#define BLUEPILL_PLUS_SWCLK_PIN         14

#endif