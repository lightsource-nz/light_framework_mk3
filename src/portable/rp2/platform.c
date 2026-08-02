// portable/rp2/platform.c
// implementation of IO routines for RP2 series MCUs

#include <light_display_ioport.h>

#include "../../light_display_ioport_internal.h"

#include <pico/time.h>
#include <hardware/i2c.h>
#include <hardware/spi.h>
#include <hardware/gpio.h>
#include <hardware/dma.h>

#define I2C_BAUDRATE                    (300 * 1000)
#define SPI_BAUDRATE                    (10 * 1000 * 1000)

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
        if(io->pin_reset != LIGHT_DISPLAY_IOPORT_PIN_NONE) {
                gpio_set_function(io->pin_reset, GPIO_FUNC_SIO);
                gpio_set_dir(io->pin_reset, true);
        }
        uint rate = i2c_init(port, I2C_BAUDRATE);
        light_debug("i2c port id 0x%x opened at address 0x%x with baud rate %d", io->port_id, io->io.i2c.addr, rate);
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
        if(io->pin_reset != LIGHT_DISPLAY_IOPORT_PIN_NONE) {
                gpio_set_function(io->pin_reset, GPIO_FUNC_SIO);
                gpio_set_dir(io->pin_reset, true);
        }
        gpio_set_function(io->io.spi.pin_cs, GPIO_FUNC_SIO);
        gpio_set_dir(io->io.spi.pin_cs, true);
        uint rate = spi_init(port, SPI_BAUDRATE);
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
        if(io->pin_reset != LIGHT_DISPLAY_IOPORT_PIN_NONE) {
                gpio_set_function(io->pin_reset, GPIO_FUNC_SIO);
                gpio_set_dir(io->pin_reset, true);
        }
        gpio_set_function(io->io.spi.pin_cs, GPIO_FUNC_SIO);
        gpio_set_dir(io->io.spi.pin_cs, true);
        gpio_set_function(io->io.spi.pin_dc, GPIO_FUNC_SIO);
        gpio_set_dir(io->io.spi.pin_dc, true);
        uint rate = spi_init(port, SPI_BAUDRATE);
        // claimed once here and held for the io_context's whole lifetime -- not a
        // per-transfer claim/release. this framework has no module teardown path (nothing
        // calls light_module_unregister_periodic_task() meaningfully either), so there's
        // nowhere to release it even if we wanted to; 12 channels are available on RP2040
        // and this is currently the only DMA user, so exhaustion isn't a concern
        io->io.spi.dma_channel = dma_claim_unused_channel(true);
        light_debug("spi port id 0x%x opened with baud rate %d, dma channel %d", io->port_id, rate, io->io.spi.dma_channel);
}

void _platform_signal_reset(struct io_context *io)
{
        if(io->pin_reset == LIGHT_DISPLAY_IOPORT_PIN_NONE) {
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
        i2c_write_blocking(port, io->io.i2c.addr, &control, 1, true);
        i2c_write_blocking(port, io->io.i2c.addr, &cmd, 1, false);
}
void _platform_i2c_send_data_byte(struct io_context *io, uint8_t data)
{
        i2c_inst_t *port = _i2c_select(io->port_id);
        uint8_t control = I2C_CONTROL_DATA;
        i2c_write_blocking(port, io->io.i2c.addr, &control, 1, true);
        i2c_write_blocking(port, io->io.i2c.addr, &data, 1, false);
}
// mirrors _platform_spi4_send_data_burst(): one continuous transaction covering every byte in
// 'data', instead of paying a fresh START/STOP (and control byte) per byte like
// _platform_i2c_send_data_byte() would if called len times
void _platform_i2c_send_data_burst(struct io_context *io, const uint8_t *data, uint32_t len)
{
        i2c_inst_t *port = _i2c_select(io->port_id);
        uint8_t control = I2C_CONTROL_DATA;
        i2c_write_blocking(port, io->io.i2c.addr, &control, 1, true);
        i2c_write_blocking(port, io->io.i2c.addr, data, len, false);
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
void _platform_spi3_send_command_byte(struct io_context *io, uint8_t cmd)
{

}
void _platform_spi3_send_data_byte(struct io_context *io, uint8_t data)
{

}
void _platform_spi3_send_data_burst(struct io_context *io, const uint8_t *data, uint32_t len)
{

}
// SPI3 sends are unimplemented stubs above -- these follow the same pass-through shape as
// the I2C ones for API consistency, but there's nothing to actually overlap yet either way
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
