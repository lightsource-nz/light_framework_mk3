#include <light.h>
#include <light_platform.h>

//   light_platform's PWM on STM32, backed by the general-purpose and advanced timers.
//
//   PIN -> TIMER IS A TABLE, NOT A FORMULA. On RP2 a GPIO's PWM slice and channel are
// arithmetic on the pin number; on STM32 the mapping is an arbitrary matrix in the datasheet,
// different per part, and most pins reach several timers through different alternate
// functions. So this carries an explicit table of the mappings that have actually been checked
// against the reference manual, rather than a derivation that would be wrong in ways nothing
// catches. A pin that is not in the table returns NULL from open(), which every consumer
// already handles -- it is the same answer a host build gives.
//
//   COMPLEMENTARY OUTPUTS are supported because this board needs one: the MiniSTM32H7xx drives
// its backlight from PE10, which is TIM1_CH2N rather than a plain channel. Those need two
// things a normal channel does not -- CCxNE instead of CCxE, and BDTR.MOE, without which an
// advanced timer's outputs stay electrically off no matter what else is configured.

#define PWM_MAX_INSTANCES       8

struct lp_pwm {
        TIM_TypeDef *tim;
        uint8_t channel;                // 1..4
        bool complementary;             // drive CHxN rather than CHx
        uint8_t pin;                    // packed port|pin, as light_ioport encodes them
        uint8_t group;                  // timer number, for logging
        uint8_t af;                     // re-asserted by configure() -- see release_pin()
        uint16_t wrap;
        bool in_use;
};

struct pwm_pin_map {
        uint8_t pin;
        TIM_TypeDef *tim;
        uint8_t channel;
        bool complementary;
        uint8_t af;
        uint8_t group;
};

// same packing light_ioport uses: high nibble port (0=A), low nibble pin
#define PWM_PIN(port_letter, pin_number) \
        ((uint8_t)(((((port_letter) - 'A') & 0xF) << 4) | ((pin_number) & 0xF)))
#define PIN_PORT_INDEX(p)       (((p) >> 4) & 0xF)
#define PIN_NUMBER(p)           ((p) & 0xF)

#if defined(STM32H743xx)
//   TIM1 on port E, AF1 -- the full set, since they come as a block in the datasheet's
// alternate-function table and adding the rest costs nothing once one is verified. PE10 is the
// one this project actually drives (the MiniSTM32H7xx backlight)
static const struct pwm_pin_map _pin_map[] = {
        { PWM_PIN('E',  8), TIM1, 1, true,  1, 1 },     // TIM1_CH1N
        { PWM_PIN('E',  9), TIM1, 1, false, 1, 1 },     // TIM1_CH1
        { PWM_PIN('E', 10), TIM1, 2, true,  1, 1 },     // TIM1_CH2N -- backlight
        { PWM_PIN('E', 11), TIM1, 2, false, 1, 1 },     // TIM1_CH2
        { PWM_PIN('E', 12), TIM1, 3, true,  1, 1 },     // TIM1_CH3N
        { PWM_PIN('E', 13), TIM1, 3, false, 1, 1 },     // TIM1_CH3
        { PWM_PIN('E', 14), TIM1, 4, false, 1, 1 },     // TIM1_CH4
};
#elif defined(STM32F411xE)
// TIM1 on port A, AF1. Nothing on the F411 board drives PWM yet, so this is the minimum
// coherent set rather than an exhaustive one -- extend from the datasheet when something needs it
static const struct pwm_pin_map _pin_map[] = {
        { PWM_PIN('A',  8), TIM1, 1, false, 1, 1 },
        { PWM_PIN('A',  9), TIM1, 2, false, 1, 1 },
        { PWM_PIN('A', 10), TIM1, 3, false, 1, 1 },
        { PWM_PIN('A', 11), TIM1, 4, false, 1, 1 },
};
#else
static const struct pwm_pin_map _pin_map[] = { };
#endif

static struct lp_pwm _instances[PWM_MAX_INSTANCES];

static GPIO_TypeDef *_port_of(uint8_t pin)
{
        switch(PIN_PORT_INDEX(pin)) {
        case 0: return GPIOA;
        case 1: return GPIOB;
        case 2: return GPIOC;
        case 3: return GPIOD;
        case 4: return GPIOE;
        //   every port past E has to be guarded, not just the exotic ones: a 48-pin F411 has
        // GPIOA-E and GPIOH only, so referencing GPIOF unconditionally does not degrade
        // gracefully, it fails to compile
#ifdef GPIOF
        case 5: return GPIOF;
#endif
#ifdef GPIOG
        case 6: return GPIOG;
#endif
#ifdef GPIOH
        case 7: return GPIOH;
#endif
        default: return NULL;
        }
}
static void _port_clock_enable(uint8_t pin)
{
        uint32_t bit = 1U << PIN_PORT_INDEX(pin);
#if defined(STM32H743xx)
        RCC->AHB4ENR |= bit;
#else
        RCC->AHB1ENR |= bit;
#endif
}
static void _timer_clock_enable(TIM_TypeDef *tim)
{
        // TIM1 is on APB2 on both families. Extend alongside the pin table when a second timer
        // appears there -- a timer whose clock is off accepts every register write and does
        // nothing, which is indistinguishable from a wiring fault
        if(tim == TIM1)
                RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
        else
                light_error("no clock-enable mapping for this timer");
}
static bool _is_advanced(TIM_TypeDef *tim)
{
        // only the advanced-control timers have a break/dead-time register, and only they need
        // MOE set before any output is driven
#ifdef TIM8
        return tim == TIM1 || tim == TIM8;
#else
        return tim == TIM1;
#endif
}

struct lp_pwm *light_platform_pwm_open(uint8_t pin)
{
        const struct pwm_pin_map *map = NULL;
        for(size_t i = 0; i < (sizeof(_pin_map) / sizeof(_pin_map[0])); i++) {
                if(_pin_map[i].pin == pin) {
                        map = &_pin_map[i];
                        break;
                }
        }
        if(!map) {
                light_warn("pin 0x%x has no PWM mapping on this chip; no PWM device created", pin);
                return NULL;
        }

        struct lp_pwm *pwm = NULL;
        for(uint8_t i = 0; i < PWM_MAX_INSTANCES; i++) {
                if(!_instances[i].in_use) {
                        pwm = &_instances[i];
                        break;
                }
        }
        if(!pwm) {
                light_error("no free PWM instance for pin 0x%x", pin);
                return NULL;
        }

        pwm->tim = map->tim;
        pwm->channel = map->channel;
        pwm->complementary = map->complementary;
        pwm->pin = pin;
        pwm->group = map->group;
        pwm->af = map->af;
        pwm->wrap = 0;
        pwm->in_use = true;

        _timer_clock_enable(map->tim);
        _port_clock_enable(pin);

        GPIO_TypeDef *port = _port_of(pin);
        uint32_t n = PIN_NUMBER(pin);
        port->MODER &= ~(3U << (n * 2));
        port->MODER |= (2U << (n * 2));         // alternate function
        port->OTYPER &= ~(1U << n);
        port->OSPEEDR |= (3U << (n * 2));
        port->PUPDR &= ~(3U << (n * 2));
        port->AFR[n >> 3] &= ~(0xFU << ((n & 7) * 4));
        port->AFR[n >> 3] |= ((uint32_t)map->af << ((n & 7) * 4));

        return pwm;
}

void light_platform_pwm_close(struct lp_pwm *pwm)
{
        if(!pwm)
                return;
        light_platform_pwm_set_enabled(pwm, false);
        pwm->in_use = false;
}

void light_platform_pwm_configure(struct lp_pwm *pwm, uint16_t wrap, uint8_t clkdiv)
{
        if(!pwm)
                return;
        TIM_TypeDef *tim = pwm->tim;
        uint8_t ch = pwm->channel;

        //   re-assert the alternate function. release_pin() hands the pin back to plain GPIO
        // and nothing else would put it back -- the RP2 port learned this as a tone that
        // sounded exactly once, with no error anywhere, because the second configure() set up
        // a slice that no longer reached the pin
        GPIO_TypeDef *afport = _port_of(pwm->pin);
        if(afport) {
                uint32_t an = PIN_NUMBER(pwm->pin);
                afport->MODER &= ~(3U << (an * 2));
                afport->MODER |= (2U << (an * 2));
                afport->AFR[an >> 3] &= ~(0xFU << ((an & 7) * 4));
                afport->AFR[an >> 3] |= ((uint32_t)pwm->af << ((an & 7) * 4));
        }

        tim->CR1 &= ~TIM_CR1_CEN;
        tim->PSC = (clkdiv > 0) ? (clkdiv - 1) : 0;
        tim->ARR = wrap;
        pwm->wrap = wrap;

        //   PWM mode 1 with output-compare preload: the shadow register means a duty written
        // part-way through a period takes effect at the next update rather than producing a
        // short or stretched pulse -- visible on a backlight as a flicker on every change
        uint32_t mode = (6U << 4) | (1U << 3);   // OCxM = 110 (PWM1), OCxPE
        if(ch <= 2) {
                uint32_t shift = (ch - 1) * 8;
                tim->CCMR1 &= ~(0xFFU << shift);
                tim->CCMR1 |= (mode << shift);
        } else {
                uint32_t shift = (ch - 3) * 8;
                tim->CCMR2 &= ~(0xFFU << shift);
                tim->CCMR2 |= (mode << shift);
        }

        //   enable the output, and set its polarity so that the PIN follows the duty in the
        // conventional direction: duty 0 leaves it low, duty == wrap leaves it high.
        //   For a COMPLEMENTARY output that takes CCxNP = 1. CHxN is the inverse of the
        // channel reference by default -- which is what an H-bridge wants and the opposite of
        // what a single-ended load wants -- so without this a backlight would be brightest at
        // duty 0. light_backlight's own active_low handling then layers on top and the two
        // inversions would cancel confusingly.
        uint32_t cc_shift = (ch - 1) * 4;
        if(pwm->complementary) {
                tim->CCER &= ~(0xFU << cc_shift);
                tim->CCER |= ((TIM_CCER_CC1NE | TIM_CCER_CC1NP) << cc_shift);
        } else {
                tim->CCER &= ~(0xFU << cc_shift);
                tim->CCER |= (TIM_CCER_CC1E << cc_shift);
        }

        // an advanced timer drives nothing at all without MOE, however complete the rest is
        if(_is_advanced(tim))
                tim->BDTR |= TIM_BDTR_MOE;

        light_platform_pwm_set_duty(pwm, 0);
        tim->EGR = TIM_EGR_UG;                  // load PSC/ARR immediately
        tim->CR1 |= TIM_CR1_ARPE | TIM_CR1_CEN;
}

uint32_t light_platform_pwm_set_frequency(struct lp_pwm *pwm, uint32_t hz, uint16_t wrap)
{
        if(!pwm || !hz)
                return 0;
        //   SystemCoreClock as the timer clock is true while every prescaler is 1, which is
        // the reset state on both families. A board that configures the PLL must revisit this,
        // exactly as the console's baud divisor and the SPI clock must -- the failure is a
        // frequency that is wrong by the prescaler ratio rather than an error
        uint32_t src = SystemCoreClock;
        uint32_t div = src / ((uint32_t)(wrap + 1) * hz);
        if(div == 0)
                div = 1;
        if(div > 0x10000)
                div = 0x10000;
        light_platform_pwm_configure(pwm, wrap, (uint8_t)(div > 255 ? 255 : div));
        uint32_t actual = src / (div * (uint32_t)(wrap + 1));
        light_debug("pwm on pin 0x%x: requested %d Hz, running at %d Hz", pwm->pin, hz, actual);
        return actual;
}

void light_platform_pwm_set_duty(struct lp_pwm *pwm, uint16_t duty)
{
        if(!pwm)
                return;
        if(duty > pwm->wrap)
                duty = pwm->wrap;
        switch(pwm->channel) {
        case 1: pwm->tim->CCR1 = duty; break;
        case 2: pwm->tim->CCR2 = duty; break;
        case 3: pwm->tim->CCR3 = duty; break;
        case 4: pwm->tim->CCR4 = duty; break;
        default: break;
        }
}

void light_platform_pwm_set_enabled(struct lp_pwm *pwm, bool enabled)
{
        if(!pwm)
                return;
        if(enabled) {
                if(_is_advanced(pwm->tim))
                        pwm->tim->BDTR |= TIM_BDTR_MOE;
                pwm->tim->CR1 |= TIM_CR1_CEN;
        } else {
                // MOE cleared as well as the counter stopped: leaving MOE set would hold the
                // pin at whatever level the last compare left it on
                if(_is_advanced(pwm->tim))
                        pwm->tim->BDTR &= ~TIM_BDTR_MOE;
                pwm->tim->CR1 &= ~TIM_CR1_CEN;
        }
}

void light_platform_pwm_release_pin(struct lp_pwm *pwm, bool level)
{
        if(!pwm)
                return;
        GPIO_TypeDef *port = _port_of(pwm->pin);
        uint32_t n = PIN_NUMBER(pwm->pin);
        if(!port)
                return;
        // hands the pin back to SIO at a defined level. NOTE that a later configure() must
        // re-assert the alternate function -- the RP2 port learned this the hard way, where a
        // released pin left a tone that sounded exactly once
        port->BSRR = level ? (1U << n) : (1U << (n + 16));
        port->MODER &= ~(3U << (n * 2));
        port->MODER |= (1U << (n * 2));         // plain output
}

uint8_t light_platform_pwm_get_group(const struct lp_pwm *pwm)
{
        return pwm ? pwm->group : 0;
}
