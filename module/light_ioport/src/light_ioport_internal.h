#ifndef _LIGHT_IOPORT_INTERNAL_H
#define _LIGHT_IOPORT_INTERNAL_H

// internal function signatures for portable platform I/O interface
extern void _platform_i2c_port_init(struct io_context *io);
extern void _platform_spi3_port_init(struct io_context *io);
extern void _platform_spi4_port_init(struct io_context *io);
//   the receiving role. Only two entry points, because a slave does exactly two things: get
// configured, and be drained. No send twin -- a peer link that needs to talk back opens its own
// master context in the other direction, which is what crossfire's link does.
extern void _platform_spi_slave_port_init(struct io_context *io);
extern uint32_t _platform_spi_slave_read_available(struct io_context *io, uint8_t *out, uint32_t max);
extern void _platform_pio_spi4_port_init(struct io_context *io);

extern void _platform_signal_reset(struct io_context *io);
extern uint32_t _platform_spi_set_clock(struct io_context *io, uint32_t hz);

extern void _platform_i2c_send_command_byte(struct io_context *io, uint8_t cmd);
extern void _platform_i2c_send_data_byte(struct io_context *io, uint8_t data);
extern void _platform_i2c_send_data_burst(struct io_context *io, const uint8_t *data, uint32_t len);
extern bool _platform_i2c_read_register(struct io_context *io, uint8_t reg, uint8_t *out, uint32_t len);
extern bool _platform_i2c_write_register(struct io_context *io, uint8_t reg, const uint8_t *data, uint32_t len);
extern void _platform_i2c_send_data_burst_async(struct io_context *io, const uint8_t *data, uint32_t len);
extern bool _platform_i2c_burst_is_complete(struct io_context *io);
extern void _platform_spi3_send_command_byte(struct io_context *io, uint8_t cmd);
extern void _platform_spi3_send_data_byte(struct io_context *io, uint8_t data);
extern void _platform_spi3_send_data_burst(struct io_context *io, const uint8_t *data, uint32_t len);
extern void _platform_spi3_send_data_burst_async(struct io_context *io, const uint8_t *data, uint32_t len);
extern bool _platform_spi3_burst_is_complete(struct io_context *io);
extern void _platform_spi4_send_command_byte(struct io_context *io, uint8_t cmd);
extern void _platform_spi4_send_data_byte(struct io_context *io, uint8_t data);
extern void _platform_spi4_send_data_burst(struct io_context *io, const uint8_t *data, uint32_t len);
extern void _platform_spi4_send_data_burst_async(struct io_context *io, const uint8_t *data, uint32_t len);
extern bool _platform_spi4_burst_is_complete(struct io_context *io);
extern void _platform_pio_spi4_send_command_byte(struct io_context *io, uint8_t cmd);
extern void _platform_pio_spi4_send_data_byte(struct io_context *io, uint8_t data);
extern void _platform_pio_spi4_send_data_burst(struct io_context *io, const uint8_t *data, uint32_t len);
extern void _platform_pio_spi4_send_data_burst_async(struct io_context *io, const uint8_t *data, uint32_t len);
extern bool _platform_pio_spi4_burst_is_complete(struct io_context *io);
#endif
