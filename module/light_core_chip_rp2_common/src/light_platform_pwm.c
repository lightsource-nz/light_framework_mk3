#include <light.h>
#if(LIGHT_SYSTEM != SYSTEM_PICO_SDK)
        #error "this file should only be compiled when Pico SDK support is enabled"
#endif

#include <light_platform.h>

#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/pwm.h>

// RP2040 and RP2350 present the same hardware/pwm.h and hardware/dma.h API, so this file
// serves both chip ports rather than being copied into each. The two already carry their own
// near-identical light_platform.c, which is tolerable at ~100 lines of timer code; the DMA
// pacing-timer setup below is substantial enough that two copies would be a real maintenance
// hazard instead of a cosmetic one.
//
// It sits in a chip-common module rather than in light_core_arch_arm32_m because PWM is an
// RP2 peripheral, not an ARM architecture feature -- a future non-RP2 ARM chip sharing the
// arch module would otherwise inherit code that cannot work on it.

#define LP_PWM_MAX_INSTANCES            8

// the DAC mode used by the streaming path: undivided clock, wrapping at 255. On a 150MHz
// RP2350 that is a 586kHz carrier, far above anything audible, leaving 8 bits of duty
// resolution -- and a wrap of exactly 255 is what lets DMA write single BYTES straight into
// the compare register with no conversion on the way
#define LP_PWM_DAC_CLKDIV               1
#define LP_PWM_DAC_WRAP                 255
#define LP_PWM_DAC_SILENCE              128

struct lp_pwm {
        bool in_use;
        uint8_t pin;
        uint8_t slice;
        uint8_t channel;
        // claimed lazily, on the first streaming call: a PWM used only for brightness or
        // tones should not hold a DMA channel and a pacing timer for its whole life
        int dma_channel;
        int dma_timer;
};

static struct lp_pwm instances[LP_PWM_MAX_INSTANCES];

struct lp_pwm *light_platform_pwm_open(uint8_t pin)
{
        struct lp_pwm *pwm = NULL;
        for(uint8_t i = 0; i < LP_PWM_MAX_INSTANCES; i++) {
                if(!instances[i].in_use) {
                        pwm = &instances[i];
                        break;
                }
        }
        if(!pwm) {
                light_error("no free PWM instance for pin %d (max %d)", pin, LP_PWM_MAX_INSTANCES);
                return NULL;
        }

        pwm->in_use = true;
        pwm->pin = pin;
        pwm->dma_channel = -1;
        pwm->dma_timer = -1;
        gpio_set_function(pin, GPIO_FUNC_PWM);
        pwm->slice = (uint8_t)pwm_gpio_to_slice_num(pin);
        pwm->channel = (uint8_t)pwm_gpio_to_channel(pin);
        return pwm;
}

void light_platform_pwm_close(struct lp_pwm *pwm)
{
        if(!pwm)
                return;
        if(pwm->dma_channel >= 0) {
                if(dma_channel_is_busy(pwm->dma_channel))
                        dma_channel_abort(pwm->dma_channel);
                dma_channel_unclaim(pwm->dma_channel);
        }
        if(pwm->dma_timer >= 0)
                dma_timer_unclaim(pwm->dma_timer);
        pwm->dma_channel = -1;
        pwm->dma_timer = -1;
        pwm->in_use = false;
}

void light_platform_pwm_configure(struct lp_pwm *pwm, uint16_t wrap, uint8_t clkdiv)
{
        if(!pwm)
                return;
        if(clkdiv < 1)
                clkdiv = 1;
        pwm_config cfg = pwm_get_default_config();
        // the integer divider variant, not pwm_config_set_clkdiv(), which takes a float --
        // keeping floating point out of this path means an FPU-less RP2040 pays exactly what
        // an RP2350 does
        pwm_config_set_clkdiv_int(&cfg, clkdiv);
        pwm_config_set_wrap(&cfg, wrap);
        pwm_init(pwm->slice, &cfg, true);
}

uint32_t light_platform_pwm_set_frequency(struct lp_pwm *pwm, uint32_t hz, uint16_t wrap)
{
        if(!pwm || !hz)
                return 0;
        uint32_t sys_hz = clock_get_hz(clk_sys);
        // f = sys_clk / (clkdiv * (wrap + 1)), solved for clkdiv and rounded to nearest so
        // the error is halved relative to truncating
        uint32_t period = (uint32_t)wrap + 1;
        uint32_t divisor = (sys_hz + (hz * period) / 2) / (hz * period);
        if(divisor < 1) divisor = 1;
        // the hardware divider is 8-bit; anything past that simply cannot go slower
        if(divisor > 255) divisor = 255;
        light_platform_pwm_configure(pwm, wrap, (uint8_t)divisor);
        return sys_hz / (divisor * period);
}

void light_platform_pwm_set_duty(struct lp_pwm *pwm, uint16_t duty)
{
        if(!pwm)
                return;
        pwm_set_chan_level(pwm->slice, pwm->channel, duty);
}

void light_platform_pwm_set_enabled(struct lp_pwm *pwm, bool enabled)
{
        if(!pwm)
                return;
        pwm_set_enabled(pwm->slice, enabled);
}

void light_platform_pwm_release_pin(struct lp_pwm *pwm, bool level)
{
        if(!pwm)
                return;
        pwm_set_enabled(pwm->slice, false);
        gpio_set_function(pwm->pin, GPIO_FUNC_SIO);
        gpio_set_dir(pwm->pin, GPIO_OUT);
        gpio_put(pwm->pin, level);
}

uint8_t light_platform_pwm_get_group(const struct lp_pwm *pwm)
{
        return pwm ? pwm->slice : 0;
}

// the byte of the compare register this pin's channel drives. `cc` is one 32-bit register
// holding both channels, A in the low half, so channel B is two bytes up -- and addressing a
// single byte is valid precisely because the DAC wrap is 255, so every duty value fits in one
static volatile uint8_t *_cc_byte(struct lp_pwm *pwm)
{
        volatile uint8_t *cc = (volatile uint8_t *)&pwm_hw->slice[pwm->slice].cc;
        return cc + (pwm->channel == PWM_CHAN_B ? 2 : 0);
}

bool light_platform_pwm_stream_start(struct lp_pwm *pwm, const uint8_t *duty,
                                uint32_t count, uint32_t sample_rate)
{
        if(!pwm || !duty || !count || !sample_rate)
                return false;

        if(pwm->dma_channel < 0)
                pwm->dma_channel = dma_claim_unused_channel(false);
        if(pwm->dma_channel < 0) {
                light_error("no DMA channel available for PWM streaming on pin %d", pwm->pin);
                return false;
        }
        if(dma_channel_is_busy(pwm->dma_channel))
                return false;

        // into DAC mode, which a preceding tone will have configured away from
        gpio_set_function(pwm->pin, GPIO_FUNC_PWM);
        light_platform_pwm_configure(pwm, LP_PWM_DAC_WRAP, LP_PWM_DAC_CLKDIV);

        // a DMA pacing timer rather than a second PWM block or a sample-rate interrupt: it
        // issues a DREQ at sys_clk * X / Y for 16-bit X and Y, buying an arbitrary sample rate
        // for the cost of one timer and no CPU at all. At 22050Hz that is tens of thousands of
        // interrupts a second not being taken
        if(pwm->dma_timer < 0)
                pwm->dma_timer = dma_claim_unused_timer(false);
        if(pwm->dma_timer < 0) {
                light_error("no DMA pacing timer available for PWM streaming on pin %d", pwm->pin);
                return false;
        }
        uint32_t sys_hz = clock_get_hz(clk_sys);
        // X == 1 keeps Y inside its 16 bits for every rate this will be asked for (anything
        // above sys_clk/65535, about 2.3kHz on a 150MHz part) and holds the error down to the
        // rounding of a single divide
        uint32_t divisor = (sys_hz + sample_rate / 2) / sample_rate;
        if(divisor > 0xFFFF) divisor = 0xFFFF;
        if(divisor < 1) divisor = 1;
        dma_timer_set_fraction(pwm->dma_timer, 1, (uint16_t)divisor);

        dma_channel_config c = dma_channel_get_default_config(pwm->dma_channel);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        channel_config_set_dreq(&c, dma_get_timer_dreq(pwm->dma_timer));
        dma_channel_configure(pwm->dma_channel, &c,
                        _cc_byte(pwm),          // one compare byte, written over and over
                        duty,
                        count,
                        true);
        return true;
}

bool light_platform_pwm_stream_busy(const struct lp_pwm *pwm)
{
        if(!pwm || pwm->dma_channel < 0)
                return false;
        return dma_channel_is_busy(pwm->dma_channel);
}

void light_platform_pwm_stream_stop(struct lp_pwm *pwm)
{
        if(!pwm)
                return;
        if(pwm->dma_channel >= 0 && dma_channel_is_busy(pwm->dma_channel))
                dma_channel_abort(pwm->dma_channel);
        // parked at mid-scale rather than switched off, so stopping mid-sample settles the
        // output at zero average instead of holding it at whatever duty it had reached
        pwm_set_chan_level(pwm->slice, pwm->channel, LP_PWM_DAC_SILENCE);
}
