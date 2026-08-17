/*
 *  clock.c -- system clock tree for the STM32H743.
 *
 *  WHAT THIS REPLACES: nothing. ST's SystemInit() does not configure the PLL on H7, so the part
 *  ran on HSI at 64MHz with every prescaler at 1 -- correct, and deliberately so during bring-up
 *  because it needs no crystal to be present or working. This takes it to 400MHz off the
 *  board's crystal, and provides the 48MHz USB reference as a side effect.
 *
 *  WHY IT IS NOT JUST ABOUT SPEED. HSI is an RC oscillator, specified around +/-1% over
 *  temperature. A USB host has to hold its SOF interval to roughly +/-500ppm, so HSI48 -- the
 *  other internal oscillator -- is about an order of magnitude outside what USB requires, and
 *  CRS cannot rescue it: CRS trims against incoming SOF packets, and a host generates those
 *  rather than receiving them. A crystal-derived 48MHz is the only way to be in spec here.
 *
 *  400MHz AND NOT 480. 480 needs VOS0, which needs revision V silicon plus the SYSCFG overdrive
 *  sequence. 400 at VOS1 works on every H743 and leaves margin; the difference is not worth a
 *  configuration that boots on some units and not others.
 *
 *  ORDER IS LOAD-BEARING throughout. Voltage scaling before frequency, flash wait states before
 *  the clock that needs them, prescalers before the switch. Every one of those, done late,
 *  produces a hang rather than an error -- the core simply stops executing correctly, and the
 *  only symptom is a board that no longer responds.
 */
#include <light.h>

#include <stm32h7xx.h>

//   the crystal this board fits. Also CMSIS's HSE_VALUE default for the family, which is what
// SystemCoreClockUpdate() uses to recompute SystemCoreClock -- the two must agree or every
// derived timing, starting with the SysTick reload, is wrong by that ratio.
#define CLOCK_HSE_HZ            25000000u

//   PLL1 -> 400MHz system clock.
//     ref  = 25MHz / DIVM1(5)  = 5MHz     (PLL input range 4-8MHz, so RGE = 0b10)
//     vco  = 5MHz * DIVN1(160) = 800MHz   (wide VCO range is 192-836MHz, so VCOSEL = 0)
//     sys  = 800MHz / DIVP1(2) = 400MHz
#define PLL1_DIVM               5u
#define PLL1_DIVN               160u
#define PLL1_DIVP               2u

//   PLL3 -> exactly 48MHz for USB.
//     ref  = 25MHz / DIVM3(5)  = 5MHz
//     vco  = 5MHz * DIVN3(96)  = 480MHz
//     usb  = 480MHz / DIVQ3(10) = 48.000MHz, exactly -- no fractional divider needed
#define PLL3_DIVM               5u
#define PLL3_DIVN               96u
#define PLL3_DIVQ               10u

//   the N/P/Q fields are all "divider minus one"; DIVM is not. Getting that wrong is a silent
// frequency error rather than a failure, which is the worst kind here
#define PLL_DIVN_FIELD(n)       ((n) - 1u)
#define PLL_DIVP_FIELD(p)       ((p) - 1u)
#define PLL_DIVQ_FIELD(q)       ((q) - 1u)

//   bounded, for the same reason the USB supply wait is: a crystal that never starts -- not
// fitted, cracked, wrong load caps -- must not turn into a board that appears dead. Every wait
// here reports and falls back rather than spinning.
#define CLOCK_WAIT_SPINS        1000000u

static bool _wait(volatile uint32_t *reg, uint32_t mask, uint32_t spins)
{
        while(spins--) {
                if(*reg & mask)
                        return true;
        }
        return false;
}

void light_chip_clock_init(void)
{
        //   1. VOLTAGE SCALING FIRST. The core cannot run at 400MHz on the reset-default VOS3;
        // raising the clock first is not a slow failure but an immediate one.
        //   the LDO is already enabled by reset (PWR_CR3.LDOEN) on a board with no SMPS
        // inductor, which this one is, so the supply path itself needs nothing here.
        PWR->D3CR |= (3u << PWR_D3CR_VOS_Pos);          // 0b11 = Scale 1
        if(!_wait(&PWR->D3CR, PWR_D3CR_VOSRDY, CLOCK_WAIT_SPINS)) {
                light_error("clock: VOS1 not ready; staying on HSI at 64MHz");
                return;
        }

        //   2. the crystal. HSEBYP stays clear: this is a crystal, not an external oscillator
        // module driving the pin.
        RCC->CR |= RCC_CR_HSEON;
        if(!_wait(&RCC->CR, RCC_CR_HSERDY, CLOCK_WAIT_SPINS)) {
                //   the interesting failure, and the reason every wait here is bounded: a board
                // with no crystal fitted still boots and still works, just on HSI as before
                light_warn("clock: HSE did not start; staying on HSI at 64MHz (USB will be out of spec)");
                return;
        }

        //   3. both PLLs off the crystal, with their input dividers. PLL2 is left alone --
        // nothing in this framework uses it, and it costs nothing switched off.
        RCC->PLLCKSELR = RCC_PLLCKSELR_PLLSRC_HSE
                        | (PLL1_DIVM << RCC_PLLCKSELR_DIVM1_Pos)
                        | (PLL3_DIVM << RCC_PLLCKSELR_DIVM3_Pos);

        RCC->PLL1DIVR = PLL_DIVN_FIELD(PLL1_DIVN)
                        | (PLL_DIVP_FIELD(PLL1_DIVP) << RCC_PLL1DIVR_P1_Pos);
        RCC->PLL3DIVR = PLL_DIVN_FIELD(PLL3_DIVN)
                        | (PLL_DIVQ_FIELD(PLL3_DIVQ) << RCC_PLL3DIVR_Q3_Pos);

        //   input range 4-8MHz and the wide VCO band for both, then enable only the outputs
        // actually consumed: PLL1's P (system clock) and PLL3's Q (USB).
        RCC->PLLCFGR = (2u << RCC_PLLCFGR_PLL1RGE_Pos)
                        | (2u << RCC_PLLCFGR_PLL3RGE_Pos)
                        | RCC_PLLCFGR_DIVP1EN
                        | RCC_PLLCFGR_DIVQ3EN;

        RCC->CR |= RCC_CR_PLL1ON;
        if(!_wait(&RCC->CR, RCC_CR_PLL1RDY, CLOCK_WAIT_SPINS)) {
                light_error("clock: PLL1 did not lock; staying on HSI at 64MHz");
                return;
        }

        //   PLL3 is the USB reference and is NOT required for the system clock, so a failure
        // here is not a reason to stay slow -- light_usb checks PLL3RDY itself and falls back
        RCC->CR |= RCC_CR_PLL3ON;
        if(!_wait(&RCC->CR, RCC_CR_PLL3RDY, CLOCK_WAIT_SPINS))
                light_warn("clock: PLL3 did not lock; USB has no crystal-derived 48MHz reference");

        //   4. FLASH WAIT STATES BEFORE THE SWITCH. At VOS1 the AXI bus runs at 200MHz here,
        // which needs 2 wait states and the matching programming delay. Switching first would
        // have the core fetching from flash faster than flash can answer.
        FLASH->ACR = FLASH_ACR_LATENCY_2WS | (2u << FLASH_ACR_WRHIGHFREQ_Pos);

        //   5. bus prescalers, also before the switch, so no domain is ever briefly overclocked.
        // CPU 400 / AXI+AHB 200 / APB1-4 100, all within their VOS1 limits.
        RCC->D1CFGR = RCC_D1CFGR_HPRE_DIV2 | RCC_D1CFGR_D1PPRE_DIV2 | RCC_D1CFGR_D1CPRE_DIV1;
        RCC->D2CFGR = RCC_D2CFGR_D2PPRE1_DIV2 | RCC_D2CFGR_D2PPRE2_DIV2;
        RCC->D3CFGR = RCC_D3CFGR_D3PPRE_DIV2;

        //   6. and only now the switch
        RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL1;
        if(!_wait(&RCC->CFGR, RCC_CFGR_SWS_PLL1, CLOCK_WAIT_SPINS)) {
                light_error("clock: system clock did not switch to PLL1");
                return;
        }

        //   keeps SystemCoreClock honest, which is what light_platform_init() derives the
        // SysTick reload from -- so the millisecond tick stays a millisecond at the new speed
        SystemCoreClockUpdate();
}
