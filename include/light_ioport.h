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
// non-blocking twin of send_data_burst(): kicks the transfer off (DMA-backed where the
// platform/interface supports it) and returns immediately. 'data' must stay valid and
// unmodified until burst_is_complete() reports true -- it's read asynchronously, possibly
// well after this call returns, so it can't be a stack-local buffer that goes out of scope
extern void light_ioport_send_data_burst_async(struct io_context *io, const uint8_t *data, uint32_t len);
// call repeatedly (e.g. once per scheduler tick) until it returns true. only once true is it
// safe to reuse or free the 'data' buffer passed to the async burst call, or to start another
// transfer on the same io_context
extern bool light_ioport_burst_is_complete(struct io_context *io);
extern void light_ioport_signal_reset(struct io_context *io);

#endif
