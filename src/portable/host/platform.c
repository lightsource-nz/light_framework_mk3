// portable/host/platform.c
// implementation of IO routines for dev host platform

#include <light_display_ioport.h>

#include "../../light_display_ioport_internal.h"

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

void _platform_signal_reset(struct io_context *io)
{
        light_debug("chip reset signaled on port id 0x%x", io->port_id);
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
void _platform_spi3_send_command_byte(struct io_context *io, uint8_t cmd)
{
}
void _platform_spi3_send_data_byte(struct io_context *io, uint8_t data)
{
}
void _platform_spi3_send_data_burst(struct io_context *io, const uint8_t *data, uint32_t len)
{
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
