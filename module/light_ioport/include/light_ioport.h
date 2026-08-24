#ifndef _LIGHT_IOPORT_H
#define _LIGHT_IOPORT_H

#include <light.h>
#include <stdint.h>

#define PORT_I2C_0                      0
#define PORT_I2C_1                      1
#define PORT_I2C_2                      2

#define PORT_SPI_0                      3
#define PORT_SPI_1                      4
#define PORT_SPI_2                      5

// PIO-emulated SPI master, for when both real hardware SPI peripherals are already
// claimed by something else (e.g. crossfire's SPI link test rig, which needs spi0 as a
// link-out master and spi1 as a link-in slave, leaving no hardware SPI free for a
// display) -- port_id here selects which PIO block + state machine to claim, not a
// hardware SPI instance
#define PORT_PIO_SPI_0                  6
#define PORT_PIO_SPI_1                  7

#define IO_I2C                          0
#define IO_SPI_4P                       1
#define IO_SPI_3P                       2
#define IO_PIO_SPI_4P                   3
//   the one RECEIVING role here. Every other io_type is a master that this device drives; this
// one is a slave, clocked by whatever is on the other end of the wire, and it is read rather
// than written. It exists because a board-to-board link is not a display transport -- see
// light_ioport_setup_io_spi_slave() below.
#define IO_SPI_SLAVE                    4

// pass as pin_reset to any setup_io_*() call for boards with no hardware RESET line broken
// out (common on simple I2C breakouts) -- 0xFF is never a valid RP2040 GPIO number, so it's
// safe to use as a sentinel. signal_reset() becomes a no-op for a context set up this way,
// and the reset pin is left unconfigured rather than driven
#define LIGHT_IOPORT_PIN_NONE   0xFF

struct i2c_state {
        uint8_t pin_scl;
        uint8_t pin_sda;
        // 7-bit slave address (e.g. 0x3C for the common default SSD1306/SH1106-family
        // address, 0x3D if the display's SA0/D-C# pin is tied high) -- I2C has no chip-select
        // line, so this is what identifies the target device on the shared bus instead
        uint8_t addr;
};
struct spi_state {
        uint8_t pin_sck;
        uint8_t pin_cs;
        uint8_t pin_dc;
        uint8_t pin_mosi;
        // DMA channel used for async burst sends, claimed once (RP2 platform only) when the
        // io_context is set up and held for its whole lifetime -- not claimed/released per
        // transfer. -1 on platforms/interfaces that don't back async sends with DMA
        int dma_channel;
        // the clock this context last asked the peripheral for, in Hz. defaulted to the
        // platform's own baud rate at setup, so a caller that never touches it sees no
        // change (see light_ioport_set_spi_clock())
        uint32_t clock_hz;
        // SPI clock polarity and phase, 0-3 (see LIGHT_IOPORT_SPI_MODE_*). Defaults to mode 0
        // at setup, which is what every display this has driven expects
        uint8_t mode;
        //   SLAVE RECEIVE RING, null unless light_ioport_spi_slave_set_rx_buffer() supplied one.
        // Filled by the peripheral's receive interrupt and drained by read_available(), which
        // makes it a single-producer/single-consumer queue -- head is written only by the ISR and
        // tail only by the reader, so neither needs a lock, and both must stay volatile because
        // each side is modified by something the compiler cannot see from the other.
        uint8_t *rx_buf;
        uint32_t rx_mask;                       // rx_buf length - 1; the length is a power of two
        volatile uint32_t rx_head;              // ISR writes here
        volatile uint32_t rx_tail;              // reader consumes from here
        // set by the ISR when a byte arrives with the ring already full, and never cleared by it
        // -- read_available() reports and clears, so a dropped byte cannot pass unnoticed
        volatile bool rx_overflow;
};
struct io_context {
        uint8_t io_type;
        uint8_t port_id;
        union {
                struct i2c_state i2c;
                struct spi_state spi;
        } io;
        uint8_t pin_reset;
};

//   PIN NUMBERING IS PLATFORM-SPECIFIC. On RP2 a pin is the flat GPIO number the SDK uses. On
// STM32 a pin is a (port, pin) pair, and both are packed into the same uint8_t: high nibble is
// the port index (0=A .. 10=K), low nibble is the pin, 0-15. Write it with the macro rather
// than as a constant -- 0x4C is not recognisably PE12, and a wrong nibble drives an unrelated
// pin rather than failing.
#define LIGHT_IOPORT_PIN_STM32(port_letter, pin_number) \
        ((uint8_t)(((((port_letter) - 'A') & 0xF) << 4) | ((pin_number) & 0xF)))
// for a device whose reset line is not on a GPIO at all -- tied to the board's reset, or to a
// fixed rail. light_ioport_signal_reset() then skips the pulse instead of driving whatever pin
// the encoding would otherwise decode to
#define LIGHT_IOPORT_PIN_NONE           0xFF

// unlike the SPI variants, there is no pin_cs here -- I2C addresses its target over the bus
// itself (see 'addr' below) rather than with a dedicated chip-select line
extern struct io_context *light_ioport_setup_io_i2c(
                        uint8_t port_id, uint8_t pin_reset, uint8_t addr, uint8_t pin_scl, uint8_t pin_sda);
extern struct io_context *light_ioport_setup_io_spi_4p(
                        uint8_t port_id, uint8_t pin_reset, uint8_t pin_cs, uint8_t pin_dc, uint8_t pin_sck, uint8_t pin_mosi);
extern struct io_context *light_ioport_setup_io_spi_3p(
                        uint8_t port_id, uint8_t pin_reset, uint8_t pin_cs, uint8_t pin_sck, uint8_t pin_mosi);
extern struct io_context *light_ioport_setup_io_pio_spi_4p(
                        uint8_t port_id, uint8_t pin_reset, uint8_t pin_cs, uint8_t pin_dc, uint8_t pin_sck, uint8_t pin_mosi);
//   SPI as a SLAVE: the peripheral is clocked by the other end and this device only receives.
// There is no pin_reset, because there is no device to reset -- the far end is a peer, not a
// panel -- and no pin_dc, which is a display convention rather than an SPI one.
//
//   CS IS A PERIPHERAL INPUT here, not an output this code drives. That inversion is the whole
// difference from the master calls above and is easy to miss when copying one: setting it up as
// an output would fight the master driving the same line.
extern struct io_context *light_ioport_setup_io_spi_slave(
                        uint8_t port_id, uint8_t pin_cs, uint8_t pin_sck, uint8_t pin_mosi);
//   BUFFERS THE SLAVE RECEIVE PATH, decoupling it from how often the caller polls.
//
//   WHY THIS IS NEEDED AT ALL: without it, read_available() drains the peripheral's own FIFO and
// nothing else holds incoming bytes. Those FIFOs are tiny -- 8 bytes on an RP2 PL022, 16 on an
// H7 -- so the time the caller has to come back before data is lost is (FIFO / byte rate). At
// 7.5MHz that is about 11us on RP2, while a framework run loop polls on a 1ms tick. A slave
// cannot apply back-pressure, so anything that misses the window is simply gone, and an SPI
// link with no checksum loses it silently.
//
//   'buf' must be a POWER-OF-TWO length, stay valid for the context's lifetime, and live in
// memory the peripheral's interrupt can touch -- which on an STM32H7 means not DTCM if the
// implementation is ever moved to DMA. 256 bytes is 23ms of headroom at 7.5MHz, i.e. far more
// slack than any poll interval this framework schedules.
//
//   returns false, and leaves the context reading straight from the FIFO exactly as before, on
// any platform that has no buffered implementation. Callers that care should check.
extern bool light_ioport_spi_slave_set_rx_buffer(struct io_context *io, uint8_t *buf, uint32_t len);
// I/O functions are all blocking, for now
extern void light_ioport_send_command_byte(struct io_context *io, uint8_t cmd);
extern void light_ioport_send_data_byte(struct io_context *io, uint8_t data);
// sends 'len' data bytes as one continuous transfer: CS is asserted once, held for the
// whole burst, then deasserted -- for SPI, dramatically cheaper than 'len' individual
// send_data_byte() calls, each of which pays its own CS/GPIO toggle overhead. safe to
// use for any run of bytes the chip is expected to auto-increment its own internal
// address through (e.g. one SH1107 page-write burst); callers still issue their own
// address-setup commands before the burst
extern void light_ioport_send_data_burst(struct io_context *io, const uint8_t *data, uint32_t len);
// reads 'len' bytes starting at register 'reg' into 'out' -- a write-then-read
// transaction (write the register address, then read the response, all under one held
// bus transaction on I2C). added for touch controllers, not displays, which is why every
// existing primitive above is write-only -- only the I2C path is implemented; calling
// this on a SPI io_context returns false and leaves 'out' untouched
extern bool light_ioport_read_register(struct io_context *io, uint8_t reg, uint8_t *out, uint32_t len);
// the write twin of the above: writes 'len' bytes into consecutive registers starting at
// 'reg', as a single transaction of [reg][data...]. like the read, this is plain I2C
// register access with no SSD1306/SH1106 control byte -- so it is NOT interchangeable with
// send_command_byte()/send_data_byte(), which prepend one. added for configuring sensors
// (an IMU has control registers to set up; a touch controller only ever had to be read),
// which is why every other write primitive here predates it and carries the display
// convention instead. only the I2C path is implemented; SPI returns false
extern bool light_ioport_write_register(struct io_context *io, uint8_t reg,
                                                const uint8_t *data, uint32_t len);
//   the 16-BIT-ADDRESS variants of the register pair above, for devices whose register
// space is addressed by two bytes rather than one (the CST328 touch controller is the
// first: its registers live at addresses like 0xD000/0xD101, sent big-endian on the
// wire). NOT a general replacement for the 8-bit pair -- a device speaks one convention
// or the other, and sending two address bytes to an 8-bit-register device writes the
// second one into a register.
//   write with len == 0 sends the bare 16-bit address as a complete transaction, STOP
// included. That is a real operation on these devices, not a degenerate case: the CST328
// takes its mode changes as address-only writes (the address IS the command), which is
// also why the 8-bit write's zero-length behaviour (address under a held START, no STOP)
// is not what is wanted here. only the I2C path is implemented; SPI returns false
extern bool light_ioport_read_register16(struct io_context *io, uint16_t reg,
                                                uint8_t *out, uint32_t len);
extern bool light_ioport_write_register16(struct io_context *io, uint16_t reg,
                                                const uint8_t *data, uint32_t len);
// non-blocking twin of send_data_burst(): kicks the transfer off (DMA-backed where the
// platform/interface supports it) and returns immediately. 'data' must stay valid and
// unmodified until burst_is_complete() reports true -- it's read asynchronously, possibly
// well after this call returns, so it can't be a stack-local buffer that goes out of scope
extern void light_ioport_send_data_burst_async(struct io_context *io, const uint8_t *data, uint32_t len);
// call repeatedly (e.g. once per scheduler tick) until it returns true. only once true is it
// safe to reuse or free the 'data' buffer passed to the async burst call, or to start another
// transfer on the same io_context
extern bool light_ioport_burst_is_complete(struct io_context *io);
//   drains whatever the peripheral has already received into 'out', up to 'max' bytes, and
// returns how many were taken. NON-BLOCKING and never waits for a byte to arrive: zero is an
// ordinary answer meaning nothing has been clocked in since the last call.
//
//   partial reads are expected, not exceptional. A slave has no say in when its master clocks
// bytes, so a caller waiting for an N-byte frame will normally see it arrive across several
// calls and must accumulate. Draining what is there and returning is what lets that caller stay
// in a polling loop rather than blocking a scheduler tick on a peer that may say nothing at all.
//
//   only IO_SPI_SLAVE implements this; every other io_type is a master and returns 0.
extern uint32_t light_ioport_read_available(struct io_context *io, uint8_t *out, uint32_t max);
extern void light_ioport_signal_reset(struct io_context *io);
//   the same reset line, driven one edge at a time and WITHOUT sleeping. signal_reset() above
// is a bring-up routine: it holds the line and blocks for the chip's boot time, which is fine
// during module load and ruinous anywhere else -- called from a periodic task it stops the whole
// scheduler loop, display and all, for as long as the pulse lasts.
//   a driver that has to reset a chip while the application is running uses this instead and
// times the phases against its own poll, staying off the bus until the chip is back. `asserted`
// means "hold this chip in reset", so the polarity of the actual pin lives here rather than in
// every caller. A context set up with LIGHT_IOPORT_PIN_NONE has no line to drive and this does
// nothing, exactly as signal_reset() does nothing for it
extern void light_ioport_set_reset(struct io_context *io, bool asserted);
// re-clocks the SPI peripheral this context talks through, returning the rate actually
// achieved (the divider is integer, so it is rarely exactly what was asked for).
//
// the honest caveat: this sets the PERIPHERAL's baud rate, not something private to this
// context -- two devices sharing one SPI port share the clock, and the last caller wins. it
// is per-context only in the sense that a context is how you name the port. it exists
// because the alternative is the single global SPI_BAUDRATE, and raising that silently
// re-clocks every display in the tree including ones on hardware nobody can currently test.
//
// panels vary widely in what they tolerate, and too high a clock shows up as corrupt pixels
// rather than a clean failure, so step it up and look at the glass. no-op on a non-SPI
// context
extern uint32_t light_ioport_set_spi_clock(struct io_context *io, uint32_t hz);

//   SPI mode: the (CPOL, CPHA) pair, named the conventional way. Mode 0 is the default and is
// what displays expect, so nothing that predates this call needs to change.
//
//   MODE 1 IS NOT AN AESTHETIC CHOICE FOR A BOARD-TO-BOARD LINK. With CPHA=0 an RP2 SPI slave
// (an ARM PL022) takes the CS falling edge as the start of a frame, so a master that holds CS
// low across a multi-byte burst gets its first byte through and zeros after it -- the byte
// count and the clock both look perfectly correct, which is what makes it hard to see. CPHA=1
// is the documented way to run back-to-back frames under one CS assertion. Both ends of a link
// must agree, and unlike the clock, a slave DOES need this set: phase is not something the
// master can impose on it.
#define LIGHT_IOPORT_SPI_MODE_0         0       // CPOL=0 CPHA=0
#define LIGHT_IOPORT_SPI_MODE_1         1       // CPOL=0 CPHA=1
#define LIGHT_IOPORT_SPI_MODE_2         2       // CPOL=1 CPHA=0
#define LIGHT_IOPORT_SPI_MODE_3         3       // CPOL=1 CPHA=1
extern void light_ioport_set_spi_mode(struct io_context *io, uint8_t mode);

#endif
