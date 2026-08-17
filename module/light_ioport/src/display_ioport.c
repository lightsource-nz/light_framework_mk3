#include <light_ioport.h>

#include "light_ioport_internal.h"

// I/O port abstractions:
//      for now we just hard-code mappings for RP2040 targets.
//      TODO add a decoupling layer to support other targets
struct io_context *light_ioport_setup_io_i2c(
                uint8_t port_id, uint8_t pin_reset, uint8_t addr, uint8_t pin_scl, uint8_t pin_sda)
{
        // ASSERT  _port_is_i2c(port_id)
        struct io_context *io = light_alloc(sizeof(*io));
        io->io_type = IO_I2C;
        io->port_id = port_id;
        // previously missing: light_ioport_signal_reset()/_platform_signal_reset()
        // read io->pin_reset, but this function never stored it, leaving it uninitialised
        io->pin_reset = pin_reset;
        io->io.i2c.addr = addr;
        io->io.i2c.pin_scl = pin_scl;
        io->io.i2c.pin_sda = pin_sda;

        _platform_i2c_port_init(io);
        return io;
}
struct io_context *light_ioport_setup_io_spi_4p(
                        uint8_t port_id, uint8_t pin_reset, uint8_t pin_cs,
                        uint8_t pin_dc, uint8_t pin_sck, uint8_t pin_mosi)
{
        // ASSERT  _port_is_spi(port_id)
        struct io_context *io = light_alloc(sizeof(*io));
        io->io_type = IO_SPI_4P;
        io->port_id = port_id;
        io->pin_reset = pin_reset;
        io->io.spi.pin_sck = pin_sck;
        io->io.spi.pin_cs = pin_cs;
        io->io.spi.pin_dc = pin_dc;
        io->io.spi.pin_mosi = pin_mosi;
        _platform_spi4_port_init(io);
        return io;
}
struct io_context *light_ioport_setup_io_spi_3p(
                                uint8_t port_id, uint8_t pin_reset,
                                uint8_t pin_cs, uint8_t pin_sck, uint8_t pin_mosi)
{
        // ASSERT  _port_is_spi(port_id)
        struct io_context *io = light_alloc(sizeof(*io));
        io->io_type = IO_SPI_3P;
        io->port_id = port_id;
        io->io.spi.pin_sck = pin_sck;
        io->io.spi.pin_cs = pin_cs;
        io->io.spi.pin_mosi = pin_mosi;
        _platform_spi3_port_init(io);
        return io;
}
struct io_context *light_ioport_setup_io_pio_spi_4p(
                        uint8_t port_id, uint8_t pin_reset, uint8_t pin_cs,
                        uint8_t pin_dc, uint8_t pin_sck, uint8_t pin_mosi)
{
        struct io_context *io = light_alloc(sizeof(*io));
        io->io_type = IO_PIO_SPI_4P;
        io->port_id = port_id;
        io->pin_reset = pin_reset;
        io->io.spi.pin_sck = pin_sck;
        io->io.spi.pin_cs = pin_cs;
        io->io.spi.pin_dc = pin_dc;
        io->io.spi.pin_mosi = pin_mosi;
        _platform_pio_spi4_port_init(io);
        return io;
}

struct io_context *light_ioport_setup_io_spi_slave(
                        uint8_t port_id, uint8_t pin_cs, uint8_t pin_sck, uint8_t pin_mosi)
{
        struct io_context *io = light_alloc(sizeof(*io));
        io->io_type = IO_SPI_SLAVE;
        io->port_id = port_id;
        //   no reset line: the far end is a peer, not a device this one owns. Set explicitly
        // rather than left alone, so light_ioport_signal_reset() finds the sentinel and skips
        // instead of decoding whatever happened to be in the allocation.
        io->pin_reset = LIGHT_IOPORT_PIN_NONE;
        io->io.spi.pin_sck = pin_sck;
        io->io.spi.pin_cs = pin_cs;
        io->io.spi.pin_mosi = pin_mosi;
        //   no D/C line -- that is a display convention, not an SPI one
        io->io.spi.pin_dc = LIGHT_IOPORT_PIN_NONE;
        _platform_spi_slave_port_init(io);
        return io;
}

uint32_t light_ioport_read_available(struct io_context *io, uint8_t *out, uint32_t max)
{
        switch(io->io_type) {
        case IO_SPI_SLAVE:
                return _platform_spi_slave_read_available(io, out, max);
        default:
                //   every other type is a master. Returning 0 rather than warning: a caller
                // polling a master context gets "nothing arrived", which is true, and this is
                // called from run loops where a per-call log would flood the console
                return 0;
        }
}

void light_ioport_send_command_byte(struct io_context *io, uint8_t cmd)
{
        // trace, not debug -- fires multiple times per frame on any display driver that
        // redraws continuously (e.g. CASET/RASET/RAMWR every update()), flooding the
        // console at DEBUG level otherwise
        light_trace("command: 0x%x", cmd);
        switch(io->io_type) {
        case IO_I2C:
                _platform_i2c_send_command_byte(io, cmd);
                break;
        case IO_SPI_3P:
                _platform_spi3_send_command_byte(io, cmd);
                break;
        case IO_SPI_4P:
                _platform_spi4_send_command_byte(io, cmd);
                break;
        case IO_PIO_SPI_4P:
                _platform_pio_spi4_send_command_byte(io, cmd);
                break;
        default:
                light_warn("invalid I/O context type code: 0x%x", io->io_type);
        }
}
void light_ioport_send_data_byte(struct io_context *io, uint8_t data)
{
        light_trace ("data: 0x%x", data);
        switch(io->io_type) {
        case IO_I2C:
                _platform_i2c_send_data_byte(io, data);
                break;
        case IO_SPI_3P:
                _platform_spi3_send_data_byte(io, data);
                break;
        case IO_SPI_4P:
                _platform_spi4_send_data_byte(io, data);
                break;
        case IO_PIO_SPI_4P:
                _platform_pio_spi4_send_data_byte(io, data);
                break;
        default:
                light_warn("invalid I/O context type code: 0x%x", io->io_type);
        }
}
void light_ioport_send_data_burst(struct io_context *io, const uint8_t *data, uint32_t len)
{
        light_trace("burst: %d bytes", len);
        switch(io->io_type) {
        case IO_I2C:
                _platform_i2c_send_data_burst(io, data, len);
                break;
        case IO_SPI_3P:
                _platform_spi3_send_data_burst(io, data, len);
                break;
        case IO_SPI_4P:
                _platform_spi4_send_data_burst(io, data, len);
                break;
        case IO_PIO_SPI_4P:
                _platform_pio_spi4_send_data_burst(io, data, len);
                break;
        default:
                light_warn("invalid I/O context type code: 0x%x", io->io_type);
        }
}
void light_ioport_send_data_burst_async(struct io_context *io, const uint8_t *data, uint32_t len)
{
        light_trace("burst async: %d bytes", len);
        switch(io->io_type) {
        case IO_I2C:
                _platform_i2c_send_data_burst_async(io, data, len);
                break;
        case IO_SPI_3P:
                _platform_spi3_send_data_burst_async(io, data, len);
                break;
        case IO_SPI_4P:
                _platform_spi4_send_data_burst_async(io, data, len);
                break;
        case IO_PIO_SPI_4P:
                _platform_pio_spi4_send_data_burst_async(io, data, len);
                break;
        default:
                light_warn("invalid I/O context type code: 0x%x", io->io_type);
        }
}
bool light_ioport_burst_is_complete(struct io_context *io)
{
        switch(io->io_type) {
        case IO_I2C:
                return _platform_i2c_burst_is_complete(io);
        case IO_SPI_3P:
                return _platform_spi3_burst_is_complete(io);
        case IO_SPI_4P:
                return _platform_spi4_burst_is_complete(io);
        case IO_PIO_SPI_4P:
                return _platform_pio_spi4_burst_is_complete(io);
        default:
                light_warn("invalid I/O context type code: 0x%x", io->io_type);
                return true;
        }
}
void light_ioport_signal_reset(struct io_context *io)
{
        _platform_signal_reset(io);
}
uint32_t light_ioport_set_spi_clock(struct io_context *io, uint32_t hz)
{
        switch(io->io_type) {
        case IO_SPI_3P:
        case IO_SPI_4P:
                return _platform_spi_set_clock(io, hz);
        default:
                // PIO-emulated SPI runs off a state machine clock divider set at program
                // init, not the SPI peripheral, and I2C has no business here at all
                light_warn("spi clock not settable for I/O context type code: 0x%x", io->io_type);
                return 0;
        }
}
bool light_ioport_read_register(struct io_context *io, uint8_t reg, uint8_t *out, uint32_t len)
{
        light_trace("read register: 0x%x, %d bytes", reg, len);
        switch(io->io_type) {
        case IO_I2C:
                return _platform_i2c_read_register(io, reg, out, len);
        default:
                // no display driver has ever needed a read path (SPI3/4 are write-only in
                // this codebase so far) -- not implemented rather than silently wrong
                light_warn("read not implemented for I/O context type code: 0x%x", io->io_type);
                return false;
        }
}
bool light_ioport_write_register(struct io_context *io, uint8_t reg,
                                                const uint8_t *data, uint32_t len)
{
        light_trace("write register: 0x%x, %d bytes", reg, len);
        switch(io->io_type) {
        case IO_I2C:
                return _platform_i2c_write_register(io, reg, data, len);
        default:
                // SPI register writes would need a per-controller command framing this layer
                // has no way to know -- not implemented rather than silently wrong, matching
                // the read path above
                light_warn("register write not implemented for I/O context type code: 0x%x", io->io_type);
                return false;
        }
}