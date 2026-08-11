// portable/host/platform.c
// implementation of IO routines for dev host platform

#include <light_ioport.h>

#include "../../light_ioport_internal.h"

void _platform_i2c_port_init(struct io_context *io)
{
        light_debug("i2c port id 0x%x opened", io->port_id);
}
void _platform_spi3_port_init(struct io_context *io)
{
        light_debug("spi port id 0x%x opened", io->port_id);
}
void _platform_spi4_port_init(struct io_context *io)
{
        light_debug("spi port id 0x%x opened", io->port_id);
}
void _platform_pio_spi4_port_init(struct io_context *io)
{
        light_debug("pio spi port id 0x%x opened", io->port_id);
}

void _platform_signal_reset(struct io_context *io)
{
        light_debug("chip reset signaled on port id 0x%x", io->port_id);
}
uint32_t _platform_spi_set_clock(struct io_context *io, uint32_t hz)
{
        // no peripheral to re-clock on the host; report the request back so callers that
        // check the achieved rate see something sane rather than a failure
        io->io.spi.clock_hz = hz;
        light_debug("spi port id 0x%x clock set to %d Hz (host: no-op)", io->port_id, hz);
        return hz;
}

void _platform_i2c_send_command_byte(struct io_context *io, uint8_t cmd)
{
}
void _platform_i2c_send_data_byte(struct io_context *io, uint8_t data)
{
}
void _platform_i2c_send_data_burst(struct io_context *io, const uint8_t *data, uint32_t len)
{
}
bool _platform_i2c_read_register(struct io_context *io, uint8_t reg, uint8_t *out, uint32_t len)
{
        return false;
}
bool _platform_i2c_write_register(struct io_context *io, uint8_t reg, const uint8_t *data, uint32_t len)
{
        return false;
}
void _platform_i2c_send_data_burst_async(struct io_context *io, const uint8_t *data, uint32_t len)
{
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
void _platform_spi3_send_data_burst_async(struct io_context *io, const uint8_t *data, uint32_t len)
{
}
bool _platform_spi3_burst_is_complete(struct io_context *io)
{
        return true;
}
void _platform_spi4_send_command_byte(struct io_context *io, uint8_t cmd)
{
}
void _platform_spi4_send_data_byte(struct io_context *io, uint8_t data)
{
}
void _platform_spi4_send_data_burst(struct io_context *io, const uint8_t *data, uint32_t len)
{
}
void _platform_spi4_send_data_burst_async(struct io_context *io, const uint8_t *data, uint32_t len)
{
}
bool _platform_spi4_burst_is_complete(struct io_context *io)
{
        return true;
}
void _platform_pio_spi4_send_command_byte(struct io_context *io, uint8_t cmd)
{
}
void _platform_pio_spi4_send_data_byte(struct io_context *io, uint8_t data)
{
}
void _platform_pio_spi4_send_data_burst(struct io_context *io, const uint8_t *data, uint32_t len)
{
}
void _platform_pio_spi4_send_data_burst_async(struct io_context *io, const uint8_t *data, uint32_t len)
{
}
bool _platform_pio_spi4_burst_is_complete(struct io_context *io)
{
        return true;
}
