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
// the non-blocking twin -- one edge, no delay. see light_ioport_set_reset()
extern void _platform_set_reset(struct io_context *io, bool asserted);
extern uint32_t _platform_spi_set_clock(struct io_context *io, uint32_t hz);
// applies io->io.spi.mode to the peripheral; valid for master and slave contexts alike
extern void _platform_spi_set_mode(struct io_context *io);
//   enables interrupt-driven receive into the ring already installed on the context by
// light_ioport_spi_slave_set_rx_buffer(). Returns false if the platform has no implementation,
// in which case the caller reverts the context to reading the FIFO directly.
extern bool _platform_spi_slave_start_rx(struct io_context *io);

extern void _platform_i2c_send_command_byte(struct io_context *io, uint8_t cmd);
extern void _platform_i2c_send_data_byte(struct io_context *io, uint8_t data);
extern void _platform_i2c_send_data_burst(struct io_context *io, const uint8_t *data, uint32_t len);
extern bool _platform_i2c_read_register(struct io_context *io, uint8_t reg, uint8_t *out, uint32_t len);
extern bool _platform_i2c_write_register(struct io_context *io, uint8_t reg, const uint8_t *data, uint32_t len);
// the strictly-framed single-transaction twin -- see light_ioport_write_register_byte()
extern bool _platform_i2c_write_register_byte(struct io_context *io, uint8_t reg, uint8_t value);
// 16-bit-address variants -- see light_ioport.h for the convention differences (big-endian
// address on the wire; write with len == 0 is a complete address-only transaction)
extern bool _platform_i2c_read_register16(struct io_context *io, uint16_t reg, uint8_t *out, uint32_t len);
extern bool _platform_i2c_write_register16(struct io_context *io, uint16_t reg, const uint8_t *data, uint32_t len);
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
