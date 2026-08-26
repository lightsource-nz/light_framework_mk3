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
        //   no ring until a caller supplies one. Zeroed explicitly rather than left to whatever
        // light_alloc() hands back, because the platform read path branches on rx_buf being null
        // and garbage there would be followed as a pointer.
        io->io.spi.rx_buf = NULL;
        io->io.spi.rx_mask = 0;
        io->io.spi.rx_head = 0;
        io->io.spi.rx_tail = 0;
        io->io.spi.rx_overflow = false;
        _platform_spi_slave_port_init(io);
        return io;
}

bool light_ioport_spi_slave_set_rx_buffer(struct io_context *io, uint8_t *buf, uint32_t len)
{
        if(io->io_type != IO_SPI_SLAVE) {
                light_warn("rx buffering applies to slave contexts only, not type code 0x%x", io->io_type);
                return false;
        }
        //   a power of two is what makes the ring index a mask rather than a modulo, and the ISR
        // is the hottest path in this module. Checked rather than assumed: a length of, say, 100
        // would otherwise wrap at 64 and silently discard the rest of the buffer.
        if(!buf || len < 2 || (len & (len - 1)) != 0) {
                light_warn("rx buffer length %d is not a power of two >= 2", len);
                return false;
        }
        io->io.spi.rx_buf = buf;
        io->io.spi.rx_mask = len - 1;
        io->io.spi.rx_head = 0;
        io->io.spi.rx_tail = 0;
        io->io.spi.rx_overflow = false;
        if(!_platform_spi_slave_start_rx(io)) {
                // leave the context exactly as it was, so it keeps working off the FIFO
                io->io.spi.rx_buf = NULL;
                io->io.spi.rx_mask = 0;
                return false;
        }
        light_debug("slave port id 0x%x buffering receive into %d bytes", io->port_id, len);
        return true;
}

uint32_t light_ioport_read_available(struct io_context *io, uint8_t *out, uint32_t max)
{
        switch(io->io_type) {
        case IO_SPI_SLAVE:
                //   THE BUFFERED PATH LIVES HERE, not in the platform backends, because none of
                // it is platform-specific: the ring, its indices and its overflow flag are all in
                // the io_context, and only the interrupt that fills it is per-platform. It was
                // first written inside the RP2 backend's reader, which meant the STM32 side got
                // the filling half and kept the polling half -- its ISR emptied the FIFO into the
                // ring while its reader went on polling a FIFO that was now permanently empty, so
                // read_available() returned 0 forever and the ring simply filled up and
                // overflowed. Keeping this above the backends makes that split impossible.
                if(io->io.spi.rx_buf) {
                        if(io->io.spi.rx_overflow) {
                                io->io.spi.rx_overflow = false;
                                light_error("slave port id 0x%x receive overflow; bytes were dropped",
                                                io->port_id);
                        }
                        uint32_t n = 0;
                        //   head is sampled once. Re-reading it in the condition would let the ISR
                        // extend the run mid-loop, making the amount returned depend on interrupt
                        // timing rather than on 'max'.
                        uint32_t head = io->io.spi.rx_head;
                        uint32_t tail = io->io.spi.rx_tail;
                        while(n < max && tail != head) {
                                out[n++] = io->io.spi.rx_buf[tail];
                                tail = (tail + 1u) & io->io.spi.rx_mask;
                        }
                        io->io.spi.rx_tail = tail;
                        return n;
                }
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
void light_ioport_set_reset(struct io_context *io, bool asserted)
{
        _platform_set_reset(io, asserted);
}
uint32_t light_ioport_set_spi_clock(struct io_context *io, uint32_t hz)
{
        switch(io->io_type) {
        case IO_SPI_3P:
        case IO_SPI_4P:
                return _platform_spi_set_clock(io, hz);
        case IO_SPI_SLAVE:
                //   not a failure, so not a warning: a slave is clocked entirely by the far end
                // and has no baud rate of its own to set. Returning 0 says "no clock is being
                // driven here", which is the truth
                return 0;
        default:
                // PIO-emulated SPI runs off a state machine clock divider set at program
                // init, not the SPI peripheral, and I2C has no business here at all
                light_warn("spi clock not settable for I/O context type code: 0x%x", io->io_type);
                return 0;
        }
}
void light_ioport_set_spi_mode(struct io_context *io, uint8_t mode)
{
        if(mode > LIGHT_IOPORT_SPI_MODE_3) {
                light_warn("invalid spi mode %d, ignoring", mode);
                return;
        }
        switch(io->io_type) {
        case IO_SPI_3P:
        case IO_SPI_4P:
        //   UNLIKE THE CLOCK, a slave is included here. A slave takes its clock from the far end
        // and so has no baud rate of its own, but it very much has a phase: CPHA decides which
        // clock edge it samples on, and getting it wrong is a receiver that returns nothing
        // useful while every other register reads correct.
        case IO_SPI_SLAVE:
                io->io.spi.mode = mode;
                _platform_spi_set_mode(io);
                break;
        default:
                // PIO-emulated SPI bakes its phase into the state machine program, and I2C has
                // no polarity or phase at all
                light_warn("spi mode not settable for I/O context type code: 0x%x", io->io_type);
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
bool light_ioport_write_register_byte(struct io_context *io, uint8_t reg, uint8_t value)
{
        light_trace("write register byte: 0x%x = 0x%x", reg, value);
        switch(io->io_type) {
        case IO_I2C:
                return _platform_i2c_write_register_byte(io, reg, value);
        default:
                // an I2C framing concern by definition -- SPI has no repeated START to get
                // wrong, so there is nothing for this to mean there
                light_warn("byte register write not implemented for I/O context type code: 0x%x",
                                io->io_type);
                return false;
        }
}
// the 16-bit-address pair -- see the header for what devices these exist for and how the
// zero-length write differs from the 8-bit one. dispatch mirrors the 8-bit pair exactly
bool light_ioport_read_register16(struct io_context *io, uint16_t reg, uint8_t *out, uint32_t len)
{
        light_trace("read register16: 0x%x, %d bytes", reg, len);
        switch(io->io_type) {
        case IO_I2C:
                return _platform_i2c_read_register16(io, reg, out, len);
        default:
                light_warn("read not implemented for I/O context type code: 0x%x", io->io_type);
                return false;
        }
}
bool light_ioport_write_register16(struct io_context *io, uint16_t reg,
                                                const uint8_t *data, uint32_t len)
{
        light_trace("write register16: 0x%x, %d bytes", reg, len);
        switch(io->io_type) {
        case IO_I2C:
                return _platform_i2c_write_register16(io, reg, data, len);
        default:
                light_warn("register write not implemented for I/O context type code: 0x%x", io->io_type);
                return false;
        }
}