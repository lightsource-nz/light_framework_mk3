#include <light_display_ioport.h>

#include "light_display_ioport_internal.h"

// I/O port abstractions:
//      for now we just hard-code mappings for RP2040 targets.
//      TODO add a decoupling layer to support other targets
struct io_context *light_display_ioport_setup_io_i2c(
                uint8_t port_id, uint8_t pin_reset, uint8_t pin_cs, uint8_t pin_scl, uint8_t pin_sda)
{
        // ASSERT  _port_is_i2c(port_id)
        struct io_context *io = light_alloc(sizeof(*io));
        io->io_type = IO_I2C;
        io->port_id = port_id;
        io->io.i2c.pin_scl = pin_scl;
        io->io.i2c.pin_sda = pin_sda;

        _platform_i2c_port_init(io);
        return io;
}
struct io_context *light_display_ioport_setup_io_spi_4p(
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
struct io_context *light_display_ioport_setup_io_spi_3p(
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
struct io_context *light_display_ioport_setup_io_pio_spi_4p(
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

void light_display_ioport_send_command_byte(struct io_context *io, uint8_t cmd)
{
        light_debug("command: 0x%x", cmd);
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
void light_display_ioport_send_data_byte(struct io_context *io, uint8_t data)
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
void light_display_ioport_send_data_burst(struct io_context *io, const uint8_t *data, uint32_t len)
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
void light_display_ioport_signal_reset(struct io_context *io)
{
        _platform_signal_reset(io);
}