// portable/rp2/platform.c
// implementation of IO routines for RP2 series MCUs

#include <light_ioport.h>

#include "../../light_ioport_internal.h"

#include <pico/time.h>
#include <hardware/i2c.h>
#include <hardware/spi.h>
#include <hardware/gpio.h>
#include <hardware/dma.h>
#include <hardware/pio.h>
#include <hardware/clocks.h>

#include "pio_spi_write.pio.h"

#define I2C_BAUDRATE                    (300 * 1000)
#define SPI_BAUDRATE                    (10 * 1000 * 1000)

//   every I2C transfer below is bounded, because the untimed pico-sdk variants
// (i2c_write_blocking / i2c_read_blocking) wait until the end of time. On a bus shared by
// more than one device that is not a theoretical risk: a slave that holds SDA low -- the
// classic lockup, where it was mid-byte when the master stopped clocking and is still
// waiting for the clocks to finish it -- takes the whole main loop down with it, because
// nothing else runs while a task is inside one of these calls. Measured on the touch board:
// light_imu_task blocked for 8343 ms in a single call, freezing rendering, touch and audio
// with it, which is what a stuck tone and an unresponsive screen actually were.
//
//   a timeout turns that into a failed transfer, which every caller here already reports as
// false and every driver above already treats as "no sample this time".
//
//   scaled by length rather than fixed, because these calls span six-byte register reads and
// thousand-byte display bursts. At 300 kHz a byte is ~30 us on the wire, so 100 us/byte is
// better than three times the honest cost, and the base covers addressing and turnaround
#define I2C_TIMEOUT_BASE_US             2000
#define I2C_TIMEOUT_PER_BYTE_US         100
static uint _i2c_timeout_us(uint32_t len)
{
        return I2C_TIMEOUT_BASE_US + len * I2C_TIMEOUT_PER_BYTE_US;
}

// per-port_id PIO block + state machine assigned by _platform_pio_spi4_port_init() --
// looked back up by port_id in the send functions below, since struct io_context has
// nowhere else to carry PIO-specific runtime state
static struct {
        PIO pio;
        uint sm;
} _pio_spi_state[2];
// pio_spi_write_program only needs loading once per PIO block, regardless of how many
// state machines on that block end up running it -- indexed 0=pio0, 1=pio1
static int16_t _pio_spi_program_offset[2] = { -1, -1 };

static int _pio_spi_index(uint8_t port_id)
{
        switch(port_id) {
        case PORT_PIO_SPI_0:
                return 0;
        case PORT_PIO_SPI_1:
                return 1;
        default:
                return -1;
        }
}
static PIO _pio_spi_block(uint8_t port_id)
{
        switch(port_id) {
        case PORT_PIO_SPI_0:
                return pio0;
        case PORT_PIO_SPI_1:
                return pio1;
        default:
                return NULL;
        }
}

static i2c_inst_t *_i2c_select(uint8_t port_id)
{
        switch(port_id) {
        case PORT_I2C_0:
                return i2c0;
        case PORT_I2C_1:
                return i2c1;
        case PORT_I2C_2:
                return NULL;
        }

}
static spi_inst_t *_spi_select(uint8_t port_id)
{
        switch(port_id) {
        case PORT_SPI_0:
                return spi0;
        case PORT_SPI_1:
                return spi1;
        case PORT_SPI_2:
                return NULL;
        default:
                return NULL;
        }
}

uint32_t _platform_spi_set_clock(struct io_context *io, uint32_t hz)
{
        spi_inst_t *port = _spi_select(io->port_id);
        if(!port) {
                light_warn("failed: port id 0x%x is not a valid spi port", io->port_id);
                return 0;
        }
        // the divider is integer, so the achieved rate is generally not the requested one --
        // report back what the peripheral actually settled on rather than what was asked for
        uint rate = spi_set_baudrate(port, hz);
        io->io.spi.clock_hz = rate;
        light_debug("spi port id 0x%x re-clocked: requested %d Hz, running at %d Hz",
                        io->port_id, hz, rate);
        return rate;
}

void _platform_spi_set_mode(struct io_context *io)
{
        spi_inst_t *port = _spi_select(io->port_id);
        if(!port) {
                light_warn("failed: port id 0x%x is not a valid spi port", io->port_id);
                return;
        }
        //   spi_set_format() carries the data width and bit order too, so both are restated
        // here rather than left to whatever spi_init() chose -- 8-bit MSB-first is what every
        // caller of this module uses, and re-asserting it keeps mode changes from silently
        // depending on init order.
        spi_set_format(port, 8, (spi_cpol_t)((io->io.spi.mode >> 1) & 1),
                        (spi_cpha_t)(io->io.spi.mode & 1), SPI_MSB_FIRST);
        light_debug("spi port id 0x%x set to mode %d", io->port_id, io->io.spi.mode);
}

void _platform_i2c_port_init(struct io_context *io)
{
        i2c_inst_t *port;
        if(!(port = _i2c_select(io->port_id))) {
                light_warn("failed: port id 0x%x is not a valid i2c port", io->port_id);
                return;
        }
        gpio_set_function(io->io.i2c.pin_scl, GPIO_FUNC_I2C);
        gpio_set_function(io->io.i2c.pin_sda, GPIO_FUNC_I2C);
        // many small I2C OLED breakouts carry no pull-up resistors of their own, relying on
        // the host to supply them -- enable the RP2040's internal weak pull-ups so the bus
        // still works without external ones. harmless if the board already has its own
        gpio_pull_up(io->io.i2c.pin_scl);
        gpio_pull_up(io->io.i2c.pin_sda);
        // some boards (common on simple I2C breakouts) don't break out a hardware RESET line
        // at all -- leave the pin unconfigured rather than driving a GPIO that isn't actually
        // wired to anything
        if(io->pin_reset != LIGHT_IOPORT_PIN_NONE) {
                gpio_set_function(io->pin_reset, GPIO_FUNC_SIO);
                gpio_set_dir(io->pin_reset, true);
        }
        uint rate = i2c_init(port, I2C_BAUDRATE);
        light_debug("i2c port id 0x%x opened at address 0x%x with baud rate %d", io->port_id, io->io.i2c.addr, rate);
}

void _platform_spi_slave_port_init(struct io_context *io)
{
        spi_inst_t *port;
        if(!(port = _spi_select(io->port_id))) {
                light_warn("failed: port id 0x%x is not a valid spi port", io->port_id);
                return;
        }
        //   the peripheral is initialised BEFORE spi_set_slave(): spi_init() resets the block
        // and leaves it in master mode, so setting the role first would be undone here.
        uint rate = spi_init(port, SPI_BAUDRATE);
        spi_set_slave(port, true);
        io->io.spi.clock_hz = rate;

        //   CS, SCK and the data pin are ALL peripheral inputs in this role -- including CS, which
        // the master drives. Handing it to GPIO_FUNC_SPI rather than SIO is what lets the hardware
        // frame incoming words on it; configuring it as an output here, by copying the master
        // path above, would drive the line against the master.
        //
        //   pin_mosi MUST BE THE PERIPHERAL'S *RX* PIN HERE, not the pin a schematic would label
        // MOSI. The RP2 SPI block has RX and TX pins whose meaning follows the role rather than
        // the name: a master sends on TX, a slave receives on RX. Naming spi1's TX pin (GP15)
        // here does not fail loudly -- it muxes an OUTPUT onto the wire, so this board drives
        // against the peer's data line while the receiver listens on an unconnected RX pin and
        // clocks in a perfectly framed stream of 0x00. There is no field named pin_rx because
        // every other io_type here is a master, where pin_mosi is exactly right.
        gpio_set_function(io->io.spi.pin_sck, GPIO_FUNC_SPI);
        gpio_set_function(io->io.spi.pin_mosi, GPIO_FUNC_SPI);
        gpio_set_function(io->io.spi.pin_cs, GPIO_FUNC_SPI);

        // nothing async here: reads are drained by polling, so no channel is claimed
        io->io.spi.dma_channel = -1;
        light_debug("spi slave port id 0x%x opened with baud rate %d", io->port_id, rate);
}

//   the receive ISR has to get from an interrupt back to an io_context, and the SDK's handler
// signature carries no argument to pass one in. Two slots, one per hardware SPI, is the whole
// space that exists -- _spi_select() only ever answers spi0 or spi1.
static struct io_context *_rx_ctx[2];

static void _spi_slave_rx_isr(uint8_t which)
{
        struct io_context *io = _rx_ctx[which];
        if(!io)
                return;
        spi_inst_t *port = _spi_select(io->port_id);
        if(!port)
                return;

        //   drain the whole FIFO, not one byte. The interrupt is level-driven off the FIFO
        // threshold, so returning with data still in it would immediately re-enter -- and at
        // 7.5MHz that turns into an interrupt storm rather than an error.
        //   A FIFO OVERRUN IS A LOST BYTE, and on a stream with no framing that is worse than it
        // sounds: this link reassembles fixed 4-byte packets by counting, so a single dropped
        // byte slides every packet after it permanently out of alignment. Fold it into the same
        // report as a ring overflow rather than letting the peripheral's flag sit there unread.
        if(spi_get_hw(port)->ris & SPI_SSPRIS_RORRIS_BITS) {
                io->io.spi.rx_overflow = true;
                spi_get_hw(port)->icr = SPI_SSPICR_RORIC_BITS;
        }

        while(spi_is_readable(port)) {
                uint8_t b = (uint8_t) spi_get_hw(port)->dr;
                uint32_t head = io->io.spi.rx_head;
                uint32_t next = (head + 1u) & io->io.spi.rx_mask;
                if(next == io->io.spi.rx_tail) {
                        //   ring full: the byte is dropped, which is exactly what the FIFO would
                        // have done, but now it is recorded. Keep draining regardless -- leaving
                        // bytes in the FIFO would just re-trigger this interrupt forever.
                        io->io.spi.rx_overflow = true;
                        continue;
                }
                io->io.spi.rx_buf[head] = b;
                //   the byte lands BEFORE head advances. The reader treats head as "everything
                // below this is valid", so publishing the index first would expose a slot that
                // has not been written yet.
                io->io.spi.rx_head = next;
        }
        //   the receive-timeout flag latches when a partial burst sits in the FIFO below the
        // threshold, and unlike the level-driven RX flag it must be cleared by hand
        spi_get_hw(port)->icr = SPI_SSPICR_RTIC_BITS;
}
static void _spi0_slave_rx_isr(void) { _spi_slave_rx_isr(0); }
static void _spi1_slave_rx_isr(void) { _spi_slave_rx_isr(1); }

bool _platform_spi_slave_start_rx(struct io_context *io)
{
        spi_inst_t *port = _spi_select(io->port_id);
        if(!port)
                return false;

        uint8_t which = (port == spi0) ? 0 : 1;
        _rx_ctx[which] = io;

        //   START FROM A CLEAN FIFO. The peripheral is enabled by port init and the mode is set
        // after that, both well before this call, so a peer that is already transmitting has had
        // a window in which to fill the FIFO and overrun it -- observed exactly that way: 105
        // bytes through the ring, one FIFO overrun, and a packet stream one byte out of phase
        // ever after. Anything sitting here now predates buffering and cannot be part of a
        // packet this side will assemble correctly, so it is discarded rather than carried in.
        while(spi_is_readable(port))
                (void) spi_get_hw(port)->dr;
        spi_get_hw(port)->icr = SPI_SSPICR_RORIC_BITS | SPI_SSPICR_RTIC_BITS;

        //   RX and RT together. RX alone fires only once the FIFO reaches its threshold, so a
        // 4-byte packet arriving into an 8-entry FIFO with a half-full trigger would sit there
        // until the next packet pushed it over -- adding a packet of latency, or losing the last
        // one entirely if no more ever came. RT is the timeout that covers exactly that tail.
        spi_get_hw(port)->imsc = SPI_SSPIMSC_RXIM_BITS | SPI_SSPIMSC_RTIM_BITS;

        uint irq = (which == 0) ? SPI0_IRQ : SPI1_IRQ;
        irq_set_exclusive_handler(irq, (which == 0) ? _spi0_slave_rx_isr : _spi1_slave_rx_isr);
        irq_set_enabled(irq, true);
        light_debug("slave port id 0x%x receiving on irq %d", io->port_id, irq);
        return true;
}

uint32_t _platform_spi_slave_read_available(struct io_context *io, uint8_t *out, uint32_t max)
{
        //   buffered path: everything the ISR has collected since the last call. Nothing here
        // touches the peripheral, so a caller that polls slowly costs latency rather than data.
        if(io->io.spi.rx_buf) {
                if(io->io.spi.rx_overflow) {
                        io->io.spi.rx_overflow = false;
                        light_error("slave port id 0x%x receive ring overflowed; bytes were dropped",
                                        io->port_id);
                }
                uint32_t n = 0;
                //   head is sampled once. Re-reading it in the condition would let the ISR extend
                // the run mid-loop, which is harmless for correctness but makes the amount
                // returned depend on interrupt timing rather than on 'max'.
                uint32_t head = io->io.spi.rx_head;
                uint32_t tail = io->io.spi.rx_tail;
                while(n < max && tail != head) {
                        out[n++] = io->io.spi.rx_buf[tail];
                        tail = (tail + 1u) & io->io.spi.rx_mask;
                }
                io->io.spi.rx_tail = tail;
                return n;
        }

        spi_inst_t *port;
        if(!(port = _spi_select(io->port_id))) { return 0; }

        //   spi_is_readable() then a raw RX FIFO read, rather than spi_read_blocking(): the
        // blocking call also CLOCKS bytes out to obtain them, which is a master's behaviour and
        // is meaningless here -- a slave cannot generate clock. Taking only what the FIFO
        // already holds is what makes this non-blocking.
        uint32_t n = 0;
        while(n < max && spi_is_readable(port)) {
                out[n++] = (uint8_t) spi_get_hw(port)->dr;
        }
        return n;
}

void _platform_spi3_port_init(struct io_context *io)
{
        spi_inst_t *port;
        if(!(port = _spi_select(io->port_id))) {
                light_warn("failed: port id 0x%x is not a valid spi port", io->port_id);
                return;
        }
        gpio_set_function(io->io.spi.pin_sck, GPIO_FUNC_SPI);
        gpio_set_function(io->io.spi.pin_mosi, GPIO_FUNC_SPI);
        if(io->pin_reset != LIGHT_IOPORT_PIN_NONE) {
                gpio_set_function(io->pin_reset, GPIO_FUNC_SIO);
                gpio_set_dir(io->pin_reset, true);
        }
        gpio_set_function(io->io.spi.pin_cs, GPIO_FUNC_SIO);
        //   CS HIGH BEFORE IT BECOMES AN OUTPUT. the RP2 output register resets to 0, so
        // set_dir() alone leaves CS asserted from init until the first burst's trailing
        // gpio_put(true) -- meaning the first transaction of the port's life has no falling
        // edge on CS. a display mostly gets away with that; an SPI slave peer using hardware
        // NSS does not, because it takes the falling edge as the start of a transaction and
        // enables SPE with NSS already low. putting the level first also avoids a low glitch,
        // since the pin does not drive at all until set_dir() runs.
        gpio_put(io->io.spi.pin_cs, true);
        gpio_set_dir(io->io.spi.pin_cs, true);
        uint rate = spi_init(port, SPI_BAUDRATE);
        io->io.spi.clock_hz = rate;
        // SPI3 sends aren't DMA-backed (see _platform_spi3_send_data_burst_async() below) --
        // -1 makes the "unclaimed" state explicit rather than leaving light_alloc()'s
        // (unzeroed) garbage in place
        io->io.spi.dma_channel = -1;
        light_debug("spi port id 0x%x opened with baud rate %d", io->port_id, rate);
}
void _platform_spi4_port_init(struct io_context *io)
{
        spi_inst_t *port;
        if(!(port = _spi_select(io->port_id))) {
                light_warn("failed: port id 0x%x is not a valid spi port", io->port_id);
                return;
        }
        gpio_set_function(io->io.spi.pin_sck, GPIO_FUNC_SPI);
        gpio_set_function(io->io.spi.pin_mosi, GPIO_FUNC_SPI);
        if(io->pin_reset != LIGHT_IOPORT_PIN_NONE) {
                gpio_set_function(io->pin_reset, GPIO_FUNC_SIO);
                gpio_set_dir(io->pin_reset, true);
        }
        gpio_set_function(io->io.spi.pin_cs, GPIO_FUNC_SIO);
        // see _platform_spi3_port_init() for why CS is driven high before it becomes an output
        gpio_put(io->io.spi.pin_cs, true);
        gpio_set_dir(io->io.spi.pin_cs, true);
        gpio_set_function(io->io.spi.pin_dc, GPIO_FUNC_SIO);
        gpio_set_dir(io->io.spi.pin_dc, true);
        uint rate = spi_init(port, SPI_BAUDRATE);
        io->io.spi.clock_hz = rate;
        // claimed once here and held for the io_context's whole lifetime -- not a
        // per-transfer claim/release. this framework has no module teardown path (nothing
        // calls light_module_unregister_periodic_task() meaningfully either), so there's
        // nowhere to release it even if we wanted to; 12 channels are available on RP2040
        // and this is currently the only DMA user, so exhaustion isn't a concern
        io->io.spi.dma_channel = dma_claim_unused_channel(true);
        light_debug("spi port id 0x%x opened with baud rate %d, dma channel %d", io->port_id, rate, io->io.spi.dma_channel);
}

void _platform_pio_spi4_port_init(struct io_context *io)
{
        int idx = _pio_spi_index(io->port_id);
        PIO pio = _pio_spi_block(io->port_id);
        if(idx < 0 || !pio) {
                light_warn("failed: port id 0x%x is not a valid PIO SPI port", io->port_id);
                return;
        }

        gpio_set_function(io->pin_reset, GPIO_FUNC_SIO);
        gpio_set_dir(io->pin_reset, true);
        gpio_set_function(io->io.spi.pin_cs, GPIO_FUNC_SIO);
        // see _platform_spi3_port_init() for why CS is driven high before it becomes an output
        gpio_put(io->io.spi.pin_cs, true);
        gpio_set_dir(io->io.spi.pin_cs, true);
        gpio_set_function(io->io.spi.pin_dc, GPIO_FUNC_SIO);
        gpio_set_dir(io->io.spi.pin_dc, true);

        int block_idx = (pio == pio0) ? 0 : 1;
        if(_pio_spi_program_offset[block_idx] < 0)
                _pio_spi_program_offset[block_idx] = (int16_t)pio_add_program(pio, &pio_spi_write_program);
        uint offset = (uint)_pio_spi_program_offset[block_idx];

        uint sm = pio_claim_unused_sm(pio, true);
        float clkdiv = (float)clock_get_hz(clk_sys) / (SPI_BAUDRATE * 2);
        pio_spi_write_program_init(pio, sm, offset, io->io.spi.pin_mosi, io->io.spi.pin_sck, clkdiv);

        _pio_spi_state[idx].pio = pio;
        _pio_spi_state[idx].sm = sm;

        // claimed once and held for the io_context's whole lifetime, same as the
        // hardware-SPI path above -- see _platform_spi4_port_init() for why there's
        // nowhere to release it and why exhaustion isn't a concern
        io->io.spi.dma_channel = dma_claim_unused_channel(true);

        light_debug("pio spi port id 0x%x opened on pio%d sm%d, dma channel %d",
                        io->port_id, block_idx, sm, io->io.spi.dma_channel);
}

// waits not just for the FIFO to report empty, but for the state machine to actually
// finish shifting the last byte out -- FDEBUG's per-SM TXSTALL bit is set when the SM
// blocks on autopull with an empty FIFO, i.e. once it has genuinely finished (the same
// technique the RP2040/RP2350 datasheets recommend for detecting PIO TX completion)
static void _pio_spi_write_blocking(PIO pio, uint sm, const uint8_t *data, uint32_t len)
{
        for(uint32_t i = 0; i < len; i++)
                pio_sm_put_blocking(pio, sm, (uint32_t)data[i] << 24);

        uint32_t stall_mask = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm);
        pio->fdebug = stall_mask;
        while(!(pio->fdebug & stall_mask))
                tight_loop_contents();
}

void _platform_signal_reset(struct io_context *io)
{
        if(io->pin_reset == LIGHT_IOPORT_PIN_NONE) {
                // no hardware RESET line on this board -- the chip only ever resets at
                // power-on, so there's nothing to pulse here. chip_setup() still explicitly
                // programs every register it cares about, so this is fine as long as the
                // chip actually powered up cleanly
                light_debug("no reset pin configured for port id 0x%x, skipping reset pulse", io->port_id);
                return;
        }
        gpio_put(io->pin_reset, true);
        sleep_ms(100);
        gpio_put(io->pin_reset, false);
        sleep_ms(100);
        gpio_put(io->pin_reset, true);
        sleep_ms(100);
}

// SSD1306/SH1106-family I2C control byte, sent as the first byte of every transaction:
// bit 7 (Co) = 0 means every byte from here to the STOP condition is of the same kind (no
// further control bytes follow) -- both cases here always send a single uninterrupted run of
// either commands or data, so Co is always 0. bit 6 (D/C#) selects which kind: 0 for
// commands, 1 for data
#define I2C_CONTROL_COMMAND     0x00    // Co=0, D/C#=0: command stream follows
#define I2C_CONTROL_DATA        0x40    // Co=0, D/C#=1: data stream follows

void _platform_i2c_send_command_byte(struct io_context *io, uint8_t cmd)
{
        i2c_inst_t *port = _i2c_select(io->port_id);
        uint8_t control = I2C_CONTROL_COMMAND;
        // nostop=true on the control byte keeps the same START condition open across both
        // writes, so the whole thing reaches the display as one transaction (control byte
        // then the command byte), not two separate START/STOP exchanges
        i2c_write_timeout_us(port, io->io.i2c.addr, &control, 1, true, _i2c_timeout_us(1));
        i2c_write_timeout_us(port, io->io.i2c.addr, &cmd, 1, false, _i2c_timeout_us(1));
}
void _platform_i2c_send_data_byte(struct io_context *io, uint8_t data)
{
        i2c_inst_t *port = _i2c_select(io->port_id);
        uint8_t control = I2C_CONTROL_DATA;
        i2c_write_timeout_us(port, io->io.i2c.addr, &control, 1, true, _i2c_timeout_us(1));
        i2c_write_timeout_us(port, io->io.i2c.addr, &data, 1, false, _i2c_timeout_us(1));
}
// mirrors _platform_spi4_send_data_burst(): one continuous transaction covering every byte in
// 'data', instead of paying a fresh START/STOP (and control byte) per byte like
// _platform_i2c_send_data_byte() would if called len times
void _platform_i2c_send_data_burst(struct io_context *io, const uint8_t *data, uint32_t len)
{
        i2c_inst_t *port = _i2c_select(io->port_id);
        uint8_t control = I2C_CONTROL_DATA;
        i2c_write_timeout_us(port, io->io.i2c.addr, &control, 1, true, _i2c_timeout_us(1));
        i2c_write_timeout_us(port, io->io.i2c.addr, data, len, false, _i2c_timeout_us(len));
}
// write-then-read: write the register address (nostop, keeps the START condition open),
// then read the response as a separate transfer that issues its own STOP. this is plain
// I2C register access (no SSD1306/SH1106-style control byte -- that convention is
// specific to those display controllers, not a general I2C thing), which is why this
// doesn't reuse I2C_CONTROL_COMMAND/_DATA above
bool _platform_i2c_read_register(struct io_context *io, uint8_t reg, uint8_t *out, uint32_t len)
{
        i2c_inst_t *port = _i2c_select(io->port_id);
        int written = i2c_write_timeout_us(port, io->io.i2c.addr, &reg, 1, true,
                                        _i2c_timeout_us(1));
        if(written < 0)
                return false;
        int read = i2c_read_timeout_us(port, io->io.i2c.addr, out, len, false,
                                        _i2c_timeout_us(len));
        return read == (int)len;
}
// the write twin: register address then payload, both under one held START -- the same
// nostop trick the control-byte writers above use, rather than assembling [reg][data...]
// into a scratch buffer, which would impose an arbitrary maximum length here
bool _platform_i2c_write_register(struct io_context *io, uint8_t reg, const uint8_t *data, uint32_t len)
{
        i2c_inst_t *port = _i2c_select(io->port_id);
        int written = i2c_write_timeout_us(port, io->io.i2c.addr, &reg, 1, true,
                                        _i2c_timeout_us(1));
        if(written < 0)
                return false;
        // a zero-length write is a bare register-address poke, which is how some devices are
        // probed for an ACK -- the address write above already did it, so there is nothing
        // further to send, and issuing a 0-byte transfer would leave the START unterminated
        if(!len)
                return true;
        written = i2c_write_timeout_us(port, io->io.i2c.addr, data, len, false,
                                        _i2c_timeout_us(len));
        return written == (int)len;
}
// RP2040 I2C-over-DMA needs the target address folded into each FIFO command word rather
// than a simple buffer handoff (not exposed as a plain public API by hardware_i2c), which is
// meaningfully more involved than the SPI case below. rather than build that out now, this is
// a synchronous pass-through: no concurrency win on I2C yet, but it satisfies the async
// contract (burst_is_complete() is always true immediately after) so callers don't need to
// care which transport they're on
void _platform_i2c_send_data_burst_async(struct io_context *io, const uint8_t *data, uint32_t len)
{
        _platform_i2c_send_data_burst(io, data, len);
}
bool _platform_i2c_burst_is_complete(struct io_context *io)
{
        return true;
}
//   THE 3-PIN SENDS WERE EMPTY STUBS, and silently so: light_ioport_send_data_burst()
// dispatched IO_SPI_3P here and returned having done nothing at all. Any caller looked
// correct, compiled clean and moved no bytes -- crossfire's inter-board SPI link ran this way,
// with a fully configured SPI peripheral and CS pin that were simply never driven.
//   the only difference from the SPI4 versions below is that there is no D/C pin to set: a
// 3-pin port is SCK, MOSI and CS. CS frames each call, which is what gives a receiving slave
// its per-transaction delimiter.
//   NOTE this treats a "command" byte exactly like a data byte, because with no D/C line
// there is nothing at this layer to distinguish them. A 3-wire display that encodes D/C as a
// 9th bit is a different protocol and would need its own io_type rather than this one.
void _platform_spi3_send_command_byte(struct io_context *io, uint8_t cmd)
{
        gpio_put(io->io.spi.pin_cs,false);
        spi_write_blocking(_spi_select(io->port_id), &cmd, 1);
        gpio_put(io->io.spi.pin_cs,true);
}
void _platform_spi3_send_data_byte(struct io_context *io, uint8_t data)
{
        gpio_put(io->io.spi.pin_cs,false);
        spi_write_blocking(_spi_select(io->port_id), &data, 1);
        gpio_put(io->io.spi.pin_cs,true);
}
void _platform_spi3_send_data_burst(struct io_context *io, const uint8_t *data, uint32_t len)
{
        gpio_put(io->io.spi.pin_cs,false);
        spi_write_blocking(_spi_select(io->port_id), data, len);
        gpio_put(io->io.spi.pin_cs,true);
}
// SPI3 sends aren't DMA-backed -- this follows the same pass-through shape as the I2C ones
// for API consistency, but there's nothing to actually overlap yet either way
void _platform_spi3_send_data_burst_async(struct io_context *io, const uint8_t *data, uint32_t len)
{
        _platform_spi3_send_data_burst(io, data, len);
}
bool _platform_spi3_burst_is_complete(struct io_context *io)
{
        return true;
}
void _platform_spi4_send_command_byte(struct io_context *io, uint8_t cmd)
{
        gpio_put(io->io.spi.pin_dc,false);
        gpio_put(io->io.spi.pin_cs,false);

        spi_write_blocking(_spi_select(io->port_id), &cmd, 1);
        gpio_put(io->io.spi.pin_cs,true);
}
void _platform_spi4_send_data_byte(struct io_context *io, uint8_t data)
{
        gpio_put(io->io.spi.pin_dc,true);
        gpio_put(io->io.spi.pin_cs,false);

        spi_write_blocking(_spi_select(io->port_id), &data, 1);
        gpio_put(io->io.spi.pin_cs,true);
}
// holds CS low for the whole burst instead of toggling it around every byte -- each
// individual send_data_byte() call pays a fixed GPIO+SPI-transaction overhead that
// dominates for 1-byte transfers, so batching a whole run of bytes (e.g. one SH1107
// page-write burst) into a single spi_write_blocking() call is the dominant lever for
// framebuffer flush speed. DC is set once, since it doesn't change mid-burst
void _platform_spi4_send_data_burst(struct io_context *io, const uint8_t *data, uint32_t len)
{
        gpio_put(io->io.spi.pin_dc,true);
        gpio_put(io->io.spi.pin_cs,false);

        spi_write_blocking(_spi_select(io->port_id), data, len);
        gpio_put(io->io.spi.pin_cs,true);
}
// non-blocking twin of the above: asserts DC/CS exactly the same way, then hands the
// transfer to DMA instead of spi_write_blocking() and returns immediately. CS is NOT
// deasserted here -- it stays low until burst_is_complete() confirms the transfer has
// actually finished (see there for why). 'data' must stay valid until then -- it's read by
// the DMA controller asynchronously, well after this function returns
void _platform_spi4_send_data_burst_async(struct io_context *io, const uint8_t *data, uint32_t len)
{
        spi_inst_t *port = _spi_select(io->port_id);

        gpio_put(io->io.spi.pin_dc, true);
        gpio_put(io->io.spi.pin_cs, false);

        dma_channel_config c = dma_channel_get_default_config(io->io.spi.dma_channel);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
        channel_config_set_dreq(&c, spi_get_dreq(port, true));
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        dma_channel_configure(io->io.spi.dma_channel, &c,
                &spi_get_hw(port)->dr, data, len, true /* start immediately */);
}
// DMA-complete only means the FIFO has been fully *fed* -- the SPI shift register can still
// be clocking out the last byte or two after that. deasserting CS before the shift register
// truly empties would corrupt the tail of the transfer, so this checks both dma_channel_is_busy()
// (has DMA finished handing bytes to the peripheral) and spi_is_busy() (has the peripheral
// actually finished shifting them out) before reporting done and raising CS
bool _platform_spi4_burst_is_complete(struct io_context *io)
{
        spi_inst_t *port = _spi_select(io->port_id);
        if(dma_channel_is_busy(io->io.spi.dma_channel) || spi_is_busy(port))
                return false;
        gpio_put(io->io.spi.pin_cs, true);
        return true;
}
void _platform_pio_spi4_send_command_byte(struct io_context *io, uint8_t cmd)
{
        int idx = _pio_spi_index(io->port_id);
        if(idx < 0)
                return;
        gpio_put(io->io.spi.pin_dc, false);
        gpio_put(io->io.spi.pin_cs, false);
        _pio_spi_write_blocking(_pio_spi_state[idx].pio, _pio_spi_state[idx].sm, &cmd, 1);
        gpio_put(io->io.spi.pin_cs, true);
}
void _platform_pio_spi4_send_data_byte(struct io_context *io, uint8_t data)
{
        int idx = _pio_spi_index(io->port_id);
        if(idx < 0)
                return;
        gpio_put(io->io.spi.pin_dc, true);
        gpio_put(io->io.spi.pin_cs, false);
        _pio_spi_write_blocking(_pio_spi_state[idx].pio, _pio_spi_state[idx].sm, &data, 1);
        gpio_put(io->io.spi.pin_cs, true);
}
// see _platform_spi4_send_data_burst() above -- same CS-held-for-the-whole-burst
// reasoning applies here
void _platform_pio_spi4_send_data_burst(struct io_context *io, const uint8_t *data, uint32_t len)
{
        int idx = _pio_spi_index(io->port_id);
        if(idx < 0)
                return;
        gpio_put(io->io.spi.pin_dc, true);
        gpio_put(io->io.spi.pin_cs, false);
        _pio_spi_write_blocking(_pio_spi_state[idx].pio, _pio_spi_state[idx].sm, data, len);
        gpio_put(io->io.spi.pin_cs, true);
}
// non-blocking twin of the above: asserts DC/CS the same way, then hands the transfer to
// DMA and returns immediately. CS is NOT deasserted here -- it stays low until
// burst_is_complete() confirms the state machine has actually finished (see there).
// 'data' must stay valid until then: DMA reads it long after this returns.
//
// the DMA writes ONE BYTE AT A TIME straight to the TX FIFO, rather than the 32-bit
// left-justified words _pio_spi_write_blocking() pushes. that works because the program is
// configured shift-left with an autopull threshold of 8 (see pio_spi_write.pio), so the
// state machine consumes exactly one byte per FIFO word regardless of how it got there --
// the same arrangement Pico-PIO-USB's usb_tx.pio uses to DMA bytes into an identically
// configured state machine
void _platform_pio_spi4_send_data_burst_async(struct io_context *io, const uint8_t *data, uint32_t len)
{
        int idx = _pio_spi_index(io->port_id);
        if(idx < 0)
                return;
        PIO pio = _pio_spi_state[idx].pio;
        uint sm = _pio_spi_state[idx].sm;

        gpio_put(io->io.spi.pin_dc, true);
        gpio_put(io->io.spi.pin_cs, false);

        dma_channel_config c = dma_channel_get_default_config(io->io.spi.dma_channel);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
        channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        dma_channel_configure(io->io.spi.dma_channel, &c,
                &pio->txf[sm], data, len, true /* start immediately */);
}
// DMA completing only means the FIFO has been fully FED -- the state machine can still be
// shifting the last byte or two out of the OSR, and dropping CS before it finishes would
// truncate the tail of the transfer. so once DMA is done, clear the per-SM TXSTALL flag
// and wait for the SM to re-assert it, which it does as soon as it blocks on an empty
// FIFO, i.e. once it has genuinely run dry. that wait is bounded by a byte or two of bus
// time (well under a microsecond at this clock), not by the length of the burst
bool _platform_pio_spi4_burst_is_complete(struct io_context *io)
{
        int idx = _pio_spi_index(io->port_id);
        if(idx < 0)
                return true;
        if(dma_channel_is_busy(io->io.spi.dma_channel))
                return false;

        PIO pio = _pio_spi_state[idx].pio;
        uint sm = _pio_spi_state[idx].sm;
        uint32_t stall_mask = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm);
        // cleared only now, after DMA has finished: clearing at kick time instead would
        // race a momentary mid-burst stall setting it again before the transfer was
        // anywhere near complete
        pio->fdebug = stall_mask;
        while(!(pio->fdebug & stall_mask))
                tight_loop_contents();

        gpio_put(io->io.spi.pin_cs, true);
        return true;
}
