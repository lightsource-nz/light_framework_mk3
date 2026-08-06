#ifndef _LIGHT_DISPLAY_IOPORT_INTERNAL_H
#define _LIGHT_DISPLAY_IOPORT_INTERNAL_H

// internal function signatures for portable platform I/O interface
extern void _platform_i2c_port_init(struct io_context *io);
extern void _platform_spi3_port_init(struct io_context *io);
extern void _platform_spi4_port_init(struct io_context *io);
extern void _platform_pio_spi4_port_init(struct io_context *io);

extern void _platform_signal_reset(struct io_context *io);

extern void _platform_i2c_send_command_byte(struct io_context *io, uint8_t cmd);
extern void _platform_i2c_send_data_byte(struct io_context *io, uint8_t data);
extern void _platform_i2c_send_data_burst(struct io_context *io, const uint8_t *data, uint32_t len);
extern void _platform_spi3_send_command_byte(struct io_context *io, uint8_t cmd);
extern void _platform_spi3_send_data_byte(struct io_context *io, uint8_t data);
extern void _platform_spi3_send_data_burst(struct io_context *io, const uint8_t *data, uint32_t len);
extern void _platform_spi4_send_command_byte(struct io_context *io, uint8_t cmd);
extern void _platform_spi4_send_data_byte(struct io_context *io, uint8_t data);
extern void _platform_spi4_send_data_burst(struct io_context *io, const uint8_t *data, uint32_t len);
extern void _platform_pio_spi4_send_command_byte(struct io_context *io, uint8_t cmd);
extern void _platform_pio_spi4_send_data_byte(struct io_context *io, uint8_t data);
extern void _platform_pio_spi4_send_data_burst(struct io_context *io, const uint8_t *data, uint32_t len);
#endif
