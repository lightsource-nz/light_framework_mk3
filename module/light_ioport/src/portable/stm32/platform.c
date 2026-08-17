/*
 *  platform.c
 *  light_ioport platform implementation for bare-CMSIS STM32 targets
 *
 *  Only the 4-wire SPI transport is implemented. I2C, 3-wire SPI and the PIO-backed SPI are
 *  stubbed the way the host platform stubs them -- PIO in particular is an RP2 peripheral with
 *  no counterpart here, so it can never be more than a stub on this platform.
 *
 *  PIN ENCODING. The public API passes pins as uint8_t, which suits RP2's flat 0-29 GPIO
 *  numbering and does not suit STM32, where a pin is a (port, pin) pair. Both are packed into
 *  the byte: high nibble is the port index (0=A .. 10=K), low nibble is the pin, 0-15. Use
 *  LIGHT_IOPORT_PIN_STM32('E', 12) rather than writing the constant out, which is unreadable
 *  and easy to get wrong by one nibble.
 */
#include <light.h>
#include <light_ioport.h>
#include "../../light_ioport_internal.h"

#if(LIGHT_SYSTEM != SYSTEM_CMSIS)
        #error "this file should only be compiled for a bare-CMSIS target"
#endif

#include <light_core_port.h>

// --- pin helpers -------------------------------------------------------------------------

#define PIN_PORT_INDEX(p)       (((p) >> 4) & 0xF)
#define PIN_NUMBER(p)           ((p) & 0xF)
// 0xFF means "no such pin". A panel whose reset is tied to the board's own reset line, which
// is how the MiniSTM32H7xx's on-board TFT is wired, has no reset GPIO to toggle -- and
// silently toggling port A pin 15 instead (which 0xFF would otherwise decode to) is the kind
// of thing that damages something else on the board
#define PIN_NONE                0xFF

static GPIO_TypeDef *_port_of(uint8_t pin)
{
        switch(PIN_PORT_INDEX(pin)) {
        case 0: return GPIOA;
        case 1: return GPIOB;
        case 2: return GPIOC;
        case 3: return GPIOD;
        case 4: return GPIOE;
        //   guarded from F upwards, not just from I: a 48-pin F411 has GPIOA-E and GPIOH only,
        // so an unconditional GPIOF fails to compile rather than degrading
#ifdef GPIOF
        case 5: return GPIOF;
#endif
#ifdef GPIOG
        case 6: return GPIOG;
#endif
#ifdef GPIOH
        case 7: return GPIOH;
#endif
#ifdef GPIOI
        case 8: return GPIOI;
#endif
#ifdef GPIOJ
        case 9: return GPIOJ;
#endif
#ifdef GPIOK
        case 10: return GPIOK;
#endif
        default: return NULL;
        }
}

//   GPIO clocks live on different buses by family: AHB4 on H7 (the ports are in the D3
// domain), AHB1 on F4. A port whose clock is off reads back as zero and ignores writes without
// any error, so getting this wrong looks like a wiring fault rather than a software one
static void _port_clock_enable(uint8_t pin)
{
        uint32_t bit = 1U << PIN_PORT_INDEX(pin);
#if defined(STM32H743xx)
        RCC->AHB4ENR |= bit;
#else
        RCC->AHB1ENR |= bit;
#endif
}

static void _gpio_set_output(uint8_t pin)
{
        GPIO_TypeDef *port = _port_of(pin);
        uint32_t n = PIN_NUMBER(pin);
        if(!port)
                return;
        _port_clock_enable(pin);
        port->MODER &= ~(3U << (n * 2));
        port->MODER |= (1U << (n * 2));
        port->OTYPER &= ~(1U << n);
        // high speed: these lines are CS and D/C, which change on every transfer and must
        // settle before the clock edges that follow them
        port->OSPEEDR |= (3U << (n * 2));
        port->PUPDR &= ~(3U << (n * 2));
}

static void _gpio_set_af(uint8_t pin, uint32_t af)
{
        GPIO_TypeDef *port = _port_of(pin);
        uint32_t n = PIN_NUMBER(pin);
        if(!port)
                return;
        _port_clock_enable(pin);
        port->MODER &= ~(3U << (n * 2));
        port->MODER |= (2U << (n * 2));
        port->OTYPER &= ~(1U << n);
        port->OSPEEDR |= (3U << (n * 2));
        port->PUPDR &= ~(3U << (n * 2));
        // AFR[0] covers pins 0-7, AFR[1] pins 8-15, four bits each
        port->AFR[n >> 3] &= ~(0xFU << ((n & 7) * 4));
        port->AFR[n >> 3] |= (af << ((n & 7) * 4));
}

static inline void _gpio_write(uint8_t pin, bool high)
{
        GPIO_TypeDef *port = _port_of(pin);
        if(!port || pin == PIN_NONE)
                return;
        // BSRR: high half resets, low half sets. One write, no read-modify-write for an
        // interrupt to land inside
        port->BSRR = high ? (1U << PIN_NUMBER(pin)) : (1U << (PIN_NUMBER(pin) + 16));
}

// --- SPI -----------------------------------------------------------------------------------

//   port_id selects the SPI instance directly: 4 means SPI4. That mirrors the RP2 platform,
// where port_id 0/1 selects spi0/spi1, rather than inventing an index of our own.
static SPI_TypeDef *_spi_of(uint8_t port_id)
{
        switch(port_id) {
        case 1: return SPI1;
        case 2: return SPI2;
        case 3: return SPI3;
#ifdef SPI4
        case 4: return SPI4;
#endif
#ifdef SPI5
        case 5: return SPI5;
#endif
#ifdef SPI6
        case 6: return SPI6;
#endif
        default: return NULL;
        }
}

static void _spi_clock_enable(uint8_t port_id)
{
#if defined(STM32H743xx)
        switch(port_id) {
        case 1: RCC->APB2ENR |= RCC_APB2ENR_SPI1EN; break;
        case 2: RCC->APB1LENR |= RCC_APB1LENR_SPI2EN; break;
        case 3: RCC->APB1LENR |= RCC_APB1LENR_SPI3EN; break;
        case 4: RCC->APB2ENR |= RCC_APB2ENR_SPI4EN; break;
        case 5: RCC->APB2ENR |= RCC_APB2ENR_SPI5EN; break;
        case 6: RCC->APB4ENR |= RCC_APB4ENR_SPI6EN; break;
        default: break;
        }
#else
        switch(port_id) {
        case 1: RCC->APB2ENR |= RCC_APB2ENR_SPI1EN; break;
        case 2: RCC->APB1ENR |= RCC_APB1ENR_SPI2EN; break;
        case 3: RCC->APB1ENR |= RCC_APB1ENR_SPI3EN; break;
        default: break;
        }
#endif
}

// which alternate function number carries SPI on a given pin. AF5 for SPI1/2/4/5/6 and AF6 for
// SPI3 covers every mapping this port needs; a board wiring SPI somewhere exotic would need
// this widened rather than guessed at
static uint32_t _spi_af(uint8_t port_id)
{
        return (port_id == 3) ? 6U : 5U;
}

void _platform_spi4_port_init(struct io_context *io)
{
        SPI_TypeDef *spi = _spi_of(io->port_id);
        if(!spi) {
                light_error("no SPI instance for port id %d", io->port_id);
                return;
        }

        _spi_clock_enable(io->port_id);
        _gpio_set_af(io->io.spi.pin_sck, _spi_af(io->port_id));
        _gpio_set_af(io->io.spi.pin_mosi, _spi_af(io->port_id));
        // CS and D/C are driven by software, not by the peripheral's own NSS: a display
        // transaction holds CS across many bytes and toggles D/C between them, which hardware
        // NSS cannot express
        _gpio_set_output(io->io.spi.pin_cs);
        _gpio_set_output(io->io.spi.pin_dc);
        _gpio_write(io->io.spi.pin_cs, true);
        if(io->pin_reset != PIN_NONE) {
                _gpio_set_output(io->pin_reset);
                _gpio_write(io->pin_reset, true);
        }

#if defined(STM32H743xx)
        //   the H7's SPI is a different peripheral generation from the F4's, not a superset.
        // Master, 8-bit, software slave management, and a transfer size that must be programmed
        // before each transaction (CSTART is per-transfer, and TSIZE=0 means "until stopped").
        spi->CR1 = 0;
        spi->CFG1 = (7U << SPI_CFG1_DSIZE_Pos)          // 8-bit frames
                        | (io->io.spi.clock_hz ? 0U : 0U);
        spi->CFG2 = SPI_CFG2_MASTER | SPI_CFG2_SSM | SPI_CFG2_SSOE | SPI_CFG2_AFCNTR;
        spi->CR1 |= SPI_CR1_SSI;
        spi->CR1 |= SPI_CR1_SPE;
#else
        spi->CR1 = SPI_CR1_MSTR | SPI_CR1_SSI | SPI_CR1_SSM;
        spi->CR1 |= SPI_CR1_SPE;
#endif
        io->io.spi.dma_channel = -1;

        light_debug("spi port id 0x%x opened (sck=0x%x mosi=0x%x cs=0x%x dc=0x%x)",
                        io->port_id, io->io.spi.pin_sck, io->io.spi.pin_mosi,
                        io->io.spi.pin_cs, io->io.spi.pin_dc);
}

static void _spi_write_blocking(struct io_context *io, const uint8_t *data, uint32_t len)
{
        SPI_TypeDef *spi = _spi_of(io->port_id);
        if(!spi || !len)
                return;

#if defined(STM32H743xx)
        //   TSIZE must be reprogrammed per transfer, and the peripheral has to be disabled to
        // change it. Then CSTART begins the transaction. Leaving TSIZE at 0 would put it in
        // continuous mode, where it keeps clocking until stopped -- which on a display shows up
        // as trailing garbage after every write rather than as an error
        spi->CR1 &= ~SPI_CR1_SPE;
        spi->CR2 = len;
        spi->CR1 |= SPI_CR1_SPE;

        //   PRIME THE FIFO BEFORE CSTART, not after. CSTART begins clocking immediately, so
        // setting it first opens a window where the master is driving SCK with nothing to
        // shift out -- the transfer starts underrun. Filling first closes the window entirely
        // for any burst that fits in the FIFO, which every packet on the inter-board link does.
        uint32_t i = 0;
        while(i < len && (spi->SR & SPI_SR_TXP))
                *(volatile uint8_t *)&spi->TXDR = data[i++];

        spi->CR1 |= SPI_CR1_CSTART;

        // anything that did not fit is fed as the shifter drains it
        while(i < len) {
                while(!(spi->SR & SPI_SR_TXP)) { }
                *(volatile uint8_t *)&spi->TXDR = data[i++];
        }
        // EOT rather than "TX empty": the last byte has been handed to the shifter, not sent,
        // and dropping CS before it has clocked out truncates the final byte
        while(!(spi->SR & SPI_SR_EOT)) { }
        spi->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC;
        spi->CR1 &= ~SPI_CR1_SPE;
#else
        for(uint32_t i = 0; i < len; i++) {
                while(!(spi->SR & SPI_SR_TXE)) { }
                *(volatile uint8_t *)&spi->DR = data[i];
        }
        // wait for the shift register to drain, then for the peripheral to go idle
        while(!(spi->SR & SPI_SR_TXE)) { }
        while(spi->SR & SPI_SR_BSY) { }
#endif
}

void _platform_spi4_send_command_byte(struct io_context *io, uint8_t cmd)
{
        _gpio_write(io->io.spi.pin_dc, false);
        _gpio_write(io->io.spi.pin_cs, false);
        _spi_write_blocking(io, &cmd, 1);
        _gpio_write(io->io.spi.pin_cs, true);
}

void _platform_spi4_send_data_byte(struct io_context *io, uint8_t data)
{
        _gpio_write(io->io.spi.pin_dc, true);
        _gpio_write(io->io.spi.pin_cs, false);
        _spi_write_blocking(io, &data, 1);
        _gpio_write(io->io.spi.pin_cs, true);
}

void _platform_spi4_send_data_burst(struct io_context *io, const uint8_t *data, uint32_t len)
{
        // CS asserted once for the whole burst, which is the entire point of this entry point
        _gpio_write(io->io.spi.pin_dc, true);
        _gpio_write(io->io.spi.pin_cs, false);
        _spi_write_blocking(io, data, len);
        _gpio_write(io->io.spi.pin_cs, true);
}

//   no DMA yet, so the "async" send is the blocking one and completion is immediate. That is
// honest rather than lazy: light_display's chunked path polls burst_is_complete() and will
// simply never see an incomplete burst, which costs frame-rate rather than correctness. Worth
// revisiting with the H7's DMA once there is a reason to.
void _platform_spi4_send_data_burst_async(struct io_context *io, const uint8_t *data, uint32_t len)
{
        _platform_spi4_send_data_burst(io, data, len);
}
bool _platform_spi4_burst_is_complete(struct io_context *io)
{
        return true;
}

//   THE 3-PIN MASTER. This was `_unsupported("3-wire spi")` plus four empty send functions, so
// a board-to-board SPI link on this platform brought up nothing and transmitted nothing, while
// every caller looked correct -- the exact mirror of the same hole on the RP2 side. crossfire's
// H7 -> Pico link direction was dead for precisely this reason.
//   the only difference from the SPI4 entry points above is that there is no D/C pin: a 3-pin
// port is SCK, MOSI and CS. As on RP2, a "command" byte is indistinguishable from a data byte
// here, since D/C is what carried that distinction; a 3-wire display encoding D/C as a 9th bit
// is a different protocol and would need its own io_type.
void _platform_spi3_port_init(struct io_context *io)
{
        SPI_TypeDef *spi = _spi_of(io->port_id);
        if(!spi) {
                light_error("no SPI instance for port id %d", io->port_id);
                return;
        }

        _spi_clock_enable(io->port_id);
        _gpio_set_af(io->io.spi.pin_sck, _spi_af(io->port_id));
        _gpio_set_af(io->io.spi.pin_mosi, _spi_af(io->port_id));
        //   CS driven by software, and driven HIGH before anything else uses the port: a slave
        // peer with hardware NSS takes the falling edge as the start of a transaction, so a CS
        // left asserted from init means the first transfer of the port's life is never framed
        _gpio_set_output(io->io.spi.pin_cs);
        _gpio_write(io->io.spi.pin_cs, true);
        if(io->pin_reset != PIN_NONE) {
                _gpio_set_output(io->pin_reset);
                _gpio_write(io->pin_reset, true);
        }

#if defined(STM32H743xx)
        // see _platform_spi4_port_init() -- the H7 generation needs TSIZE per transfer and
        // SPE cleared while CFG1/CFG2 are written
        spi->CR1 = 0;
        spi->CFG1 = (7U << SPI_CFG1_DSIZE_Pos);
        spi->CFG2 = SPI_CFG2_MASTER | SPI_CFG2_SSM | SPI_CFG2_SSOE | SPI_CFG2_AFCNTR;
        spi->CR1 |= SPI_CR1_SSI;
        spi->CR1 |= SPI_CR1_SPE;
#else
        spi->CR1 = SPI_CR1_MSTR | SPI_CR1_SSI | SPI_CR1_SSM;
        spi->CR1 |= SPI_CR1_SPE;
#endif
        io->io.spi.dma_channel = -1;

        light_debug("3-pin spi port id 0x%x opened (sck=0x%x mosi=0x%x cs=0x%x)",
                        io->port_id, io->io.spi.pin_sck, io->io.spi.pin_mosi, io->io.spi.pin_cs);
}
void _platform_spi3_send_command_byte(struct io_context *io, uint8_t cmd)
{
        _gpio_write(io->io.spi.pin_cs, false);
        _spi_write_blocking(io, &cmd, 1);
        _gpio_write(io->io.spi.pin_cs, true);
}
void _platform_spi3_send_data_byte(struct io_context *io, uint8_t data)
{
        _gpio_write(io->io.spi.pin_cs, false);
        _spi_write_blocking(io, &data, 1);
        _gpio_write(io->io.spi.pin_cs, true);
}
//   one CS assertion around the whole burst, which is what delimits a packet for the peer.
//   THIS REQUIRES THE LINK TO RUN IN MODE 1 when the far end is an RP2 slave: with CPHA=0 a
// PL022 slave takes the CS falling edge as the start of a frame, so a burst-long assertion
// gets the first byte through and zeros after it. See light_ioport_set_spi_mode().
void _platform_spi3_send_data_burst(struct io_context *io, const uint8_t *data, uint32_t len)
{
        _gpio_write(io->io.spi.pin_cs, false);
        _spi_write_blocking(io, data, len);
        _gpio_write(io->io.spi.pin_cs, true);
}
// no DMA here either -- see _platform_spi4_send_data_burst_async() for why that is honest
void _platform_spi3_send_data_burst_async(struct io_context *io, const uint8_t *data, uint32_t len)
{
        _platform_spi3_send_data_burst(io, data, len);
}
bool _platform_spi3_burst_is_complete(struct io_context *io)
{
        return true;
}

uint32_t _platform_spi_set_clock(struct io_context *io, uint32_t hz)
{
        SPI_TypeDef *spi = _spi_of(io->port_id);
        if(!spi)
                return 0;

        //   the divider is a power of two from 2 to 256, so the achievable rate is the first
        // one at or below what was asked for -- never above it, since a display that is
        // overclocked fails intermittently rather than cleanly.
        //   kernel clock is assumed to be SystemCoreClock, true while every prescaler is 1 out
        // of reset. A board that configures the PLL must revisit this, exactly as the console's
        // baud divisor must
        uint32_t src = SystemCoreClock;
        uint32_t div = 0;
        while(div < 7 && (src >> (div + 1)) > hz)
                div++;

#if defined(STM32H743xx)
        spi->CR1 &= ~SPI_CR1_SPE;
        spi->CFG1 = (spi->CFG1 & ~SPI_CFG1_MBR_Msk) | (div << SPI_CFG1_MBR_Pos);
#else
        spi->CR1 &= ~SPI_CR1_SPE;
        spi->CR1 = (spi->CR1 & ~SPI_CR1_BR_Msk) | (div << SPI_CR1_BR_Pos);
        spi->CR1 |= SPI_CR1_SPE;
#endif
        io->io.spi.clock_hz = src >> (div + 1);
        light_debug("spi port id 0x%x re-clocked: requested %d Hz, running at %d Hz",
                        io->port_id, hz, io->io.spi.clock_hz);
        return io->io.spi.clock_hz;
}

void _platform_spi_set_mode(struct io_context *io)
{
        SPI_TypeDef *spi = _spi_of(io->port_id);
        if(!spi)
                return;

        uint32_t cpol = (io->io.spi.mode >> 1) & 1;
        uint32_t cpha = io->io.spi.mode & 1;

        //   SPE must be clear to change these on either generation, and on the H7 the whole of
        // CFG2 is write-protected while the peripheral is enabled. Whether SPE goes back on
        // depends on the role: a master re-enables it per transfer in _spi_write_blocking(),
        // but a slave has nothing that would ever switch it on again, so leaving it off there
        // would quietly disable the receiver.
        bool was_enabled = (spi->CR1 & SPI_CR1_SPE) != 0;
        spi->CR1 &= ~SPI_CR1_SPE;

#if defined(STM32H743xx)
        spi->CFG2 = (spi->CFG2 & ~(SPI_CFG2_CPOL | SPI_CFG2_CPHA))
                        | (cpol ? SPI_CFG2_CPOL : 0U)
                        | (cpha ? SPI_CFG2_CPHA : 0U);
#else
        spi->CR1 = (spi->CR1 & ~(SPI_CR1_CPOL | SPI_CR1_CPHA))
                        | (cpol ? SPI_CR1_CPOL : 0U)
                        | (cpha ? SPI_CR1_CPHA : 0U);
#endif
        if(was_enabled)
                spi->CR1 |= SPI_CR1_SPE;

        light_debug("spi port id 0x%x set to mode %d (cpol=%d cpha=%d)",
                        io->port_id, io->io.spi.mode, cpol, cpha);
}

void _platform_signal_reset(struct io_context *io)
{
        // a panel whose reset is tied to the board's own reset line has no GPIO to pulse, and
        // saying so is better than pulsing an unrelated pin
        if(io->pin_reset == PIN_NONE) {
                light_debug("io context has no reset pin; skipping reset pulse");
                return;
        }
        _gpio_write(io->pin_reset, false);
        light_platform_sleep_ms(10);
        _gpio_write(io->pin_reset, true);
        light_platform_sleep_ms(120);
}

// --- spi slave --------------------------------------------------------------------------------

//   the receiving role, for a board-to-board link rather than a display. Three differences from
// every master above, and each is easy to get wrong by copying one:
//     - NSS/CS is a peripheral INPUT driven by the far end, so it is handed to the alternate
//       function like SCK and MOSI rather than configured as an output. Driving it here would
//       fight the master on the same wire.
//     - hardware slave management (SSM clear) rather than software: the peripheral uses the CS
//       edge to frame incoming words, which is exactly the per-packet framing a link relies on.
//     - no baud rate. A slave is clocked entirely by the master; the clock_hz field is left at
//       whatever the caller set and means nothing here.
void _platform_spi_slave_port_init(struct io_context *io)
{
        SPI_TypeDef *spi = _spi_of(io->port_id);
        if(!spi) {
                light_error("no SPI instance for port id %d", io->port_id);
                return;
        }

        _spi_clock_enable(io->port_id);
        _gpio_set_af(io->io.spi.pin_sck, _spi_af(io->port_id));
        _gpio_set_af(io->io.spi.pin_mosi, _spi_af(io->port_id));
        _gpio_set_af(io->io.spi.pin_cs, _spi_af(io->port_id));

#if defined(STM32H743xx)
        //   disable before reconfiguring: CFG1/CFG2 are write-protected while SPE is set, and
        // the writes are silently dropped rather than refused
        spi->CR1 = 0;
        spi->CFG1 = (7U << SPI_CFG1_DSIZE_Pos);         // 8-bit frames
        //   MASTER clear = slave. SSM clear = hardware NSS, so the master's CS frames the words.
        // TSIZE stays 0, which on this peripheral means "no limit" -- a slave cannot know how
        // many bytes the master intends to send.
        spi->CFG2 = 0;
        spi->CR1 |= SPI_CR1_SPE;
#else
        //   the F4's classic SPI: MSTR clear = slave, SSM clear = hardware NSS. 8-bit is the
        // reset default (DFF clear), so only the bits that differ are written.
        spi->CR1 = 0;
        spi->CR1 |= SPI_CR1_SPE;
#endif
        io->io.spi.dma_channel = -1;

        light_debug("spi slave port id 0x%x opened (sck=0x%x mosi=0x%x cs=0x%x)",
                        io->port_id, io->io.spi.pin_sck, io->io.spi.pin_mosi, io->io.spi.pin_cs);
}

uint32_t _platform_spi_slave_read_available(struct io_context *io, uint8_t *out, uint32_t max)
{
        SPI_TypeDef *spi = _spi_of(io->port_id);
        if(!spi)
                return 0;

        //   drains only what has already arrived and returns; never waits for a byte. A slave
        // cannot make its master send, so blocking here would stall a run loop on a peer that
        // may be saying nothing at all.
        uint32_t n = 0;
        while(n < max) {
#if defined(STM32H743xx)
                if(!(spi->SR & SPI_SR_RXP))
                        break;
                //   an 8-BIT read of RXDR. A 32-bit access pops a whole packet regardless of
                // DSIZE, so reading the wrong width silently discards three bytes in four.
                out[n++] = *(volatile uint8_t *) &spi->RXDR;
#else
                if(!(spi->SR & SPI_SR_RXNE))
                        break;
                out[n++] = (uint8_t) spi->DR;
#endif
        }
        return n;
}

// --- unimplemented transports ----------------------------------------------------------------
//   present so that light_ioport links whichever transport a board's code refers to, and loud
// at runtime rather than silently doing nothing, which is how a display that never lights up
// becomes an afternoon of staring at wiring

static void _unsupported(const uint8_t *what)
{
        light_error("%s is not implemented on the STM32 light_ioport platform", what);
}

void _platform_i2c_port_init(struct io_context *io) { _unsupported("i2c"); }
void _platform_pio_spi4_port_init(struct io_context *io) { _unsupported("pio spi (an RP2 peripheral)"); }

void _platform_i2c_send_command_byte(struct io_context *io, uint8_t cmd) { }
void _platform_i2c_send_data_byte(struct io_context *io, uint8_t data) { }
void _platform_i2c_send_data_burst(struct io_context *io, const uint8_t *data, uint32_t len) { }
bool _platform_i2c_read_register(struct io_context *io, uint8_t reg, uint8_t *out, uint32_t len) { return false; }
bool _platform_i2c_write_register(struct io_context *io, uint8_t reg, const uint8_t *data, uint32_t len) { return false; }
void _platform_i2c_send_data_burst_async(struct io_context *io, const uint8_t *data, uint32_t len) { }
bool _platform_i2c_burst_is_complete(struct io_context *io) { return true; }

void _platform_pio_spi4_send_command_byte(struct io_context *io, uint8_t cmd) { }
void _platform_pio_spi4_send_data_byte(struct io_context *io, uint8_t data) { }
void _platform_pio_spi4_send_data_burst(struct io_context *io, const uint8_t *data, uint32_t len) { }
void _platform_pio_spi4_send_data_burst_async(struct io_context *io, const uint8_t *data, uint32_t len) { }
bool _platform_pio_spi4_burst_is_complete(struct io_context *io) { return true; }
