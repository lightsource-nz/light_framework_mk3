// portable/rp2/platform.c
// implementation of IO routines for RP2 series MCUs

#include <light_display_ioport.h>

#include "../../light_display_ioport_internal.h"

#include <pico/time.h>
#include <hardware/i2c.h>
#include <hardware/spi.h>
#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/clocks.h>

#include "pio_spi_write.pio.h"

#define I2C_BAUDRATE                    (300 * 1000)
#define SPI_BAUDRATE                    (10000 * 1000)

// per-port_id PIO block + state machine assigned by _platform_pio_spi4_port_init() --
// looked back up by port_id in the send functions below, since struct io_context has
// nowhere else to carry PIO-specific runtime state
static struct {
        PIO pio;
        uint sm;
} _pio_spi_state[2];
// pio_spi_write_program only needs loading once per PIO block, regardless of how many
// state machines on that block end up running it -- indexed 0=pio0, 1=pio1
static int16_t _pio_spi_program_offset[2] = { -1, -1 };

static int _pio_spi_index(uint8_t port_id)
{
        switch(port_id) {
        case PORT_PIO_SPI_0:
                return 0;
        case PORT_PIO_SPI_1:
                return 1;
        default:
                return -1;
        }
}
static PIO _pio_spi_block(uint8_t port_id)
{
        switch(port_id) {
        case PORT_PIO_SPI_0:
                return pio0;
        case PORT_PIO_SPI_1:
                return pio1;
        default:
                return NULL;
        }
}

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
        gpio_set_function(io->pin_reset, GPIO_FUNC_SIO);
        gpio_set_dir(io->pin_reset, true);
        uint rate = i2c_init(port, I2C_BAUDRATE);
        light_debug("i2c port id 0x%x opened with baud rate %d", io->port_id, rate);
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
        gpio_set_function(io->pin_reset, GPIO_FUNC_SIO);
        gpio_set_dir(io->pin_reset, true);
        gpio_set_function(io->io.spi.pin_cs, GPIO_FUNC_SIO);
        gpio_set_dir(io->io.spi.pin_cs, true);
        uint rate = spi_init(port, SPI_BAUDRATE);
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
        gpio_set_function(io->pin_reset, GPIO_FUNC_SIO);
        gpio_set_dir(io->pin_reset, true);
        gpio_set_function(io->io.spi.pin_cs, GPIO_FUNC_SIO);
        gpio_set_dir(io->io.spi.pin_cs, true);
        gpio_set_function(io->io.spi.pin_dc, GPIO_FUNC_SIO);
        gpio_set_dir(io->io.spi.pin_dc, true);
        uint rate = spi_init(port, SPI_BAUDRATE);
        light_debug("spi port id 0x%x opened with baud rate %d", io->port_id, rate);
}

void _platform_pio_spi4_port_init(struct io_context *io)
{
        int idx = _pio_spi_index(io->port_id);
        PIO pio = _pio_spi_block(io->port_id);
        if(idx < 0 || !pio) {
                light_warn("failed: port id 0x%x is not a valid PIO SPI port", io->port_id);
                return;
        }

        gpio_set_function(io->pin_reset, GPIO_FUNC_SIO);
        gpio_set_dir(io->pin_reset, true);
        gpio_set_function(io->io.spi.pin_cs, GPIO_FUNC_SIO);
        gpio_set_dir(io->io.spi.pin_cs, true);
        gpio_set_function(io->io.spi.pin_dc, GPIO_FUNC_SIO);
        gpio_set_dir(io->io.spi.pin_dc, true);

        int block_idx = (pio == pio0) ? 0 : 1;
        if(_pio_spi_program_offset[block_idx] < 0)
                _pio_spi_program_offset[block_idx] = (int16_t)pio_add_program(pio, &pio_spi_write_program);
        uint offset = (uint)_pio_spi_program_offset[block_idx];

        uint sm = pio_claim_unused_sm(pio, true);
        float clkdiv = (float)clock_get_hz(clk_sys) / (SPI_BAUDRATE * 2);
        pio_spi_write_program_init(pio, sm, offset, io->io.spi.pin_mosi, io->io.spi.pin_sck, clkdiv);

        _pio_spi_state[idx].pio = pio;
        _pio_spi_state[idx].sm = sm;

        light_debug("pio spi port id 0x%x opened on pio%d sm%d", io->port_id, block_idx, sm);
}

// waits not just for the FIFO to report empty, but for the state machine to actually
// finish shifting the last byte out -- FDEBUG's per-SM TXSTALL bit is set when the SM
// blocks on autopull with an empty FIFO, i.e. once it has genuinely finished (the same
// technique the RP2040/RP2350 datasheets recommend for detecting PIO TX completion)
static void _pio_spi_write_blocking(PIO pio, uint sm, const uint8_t *data, uint32_t len)
{
        for(uint32_t i = 0; i < len; i++)
                pio_sm_put_blocking(pio, sm, (uint32_t)data[i] << 24);

        uint32_t stall_mask = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm);
        pio->fdebug = stall_mask;
        while(!(pio->fdebug & stall_mask))
                tight_loop_contents();
}

void _platform_signal_reset(struct io_context *io)
{
        gpio_put(io->pin_reset, true);
        sleep_ms(100);
        gpio_put(io->pin_reset, false);
        sleep_ms(100);
        gpio_put(io->pin_reset, true);
        sleep_ms(100);
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
void _platform_pio_spi4_send_command_byte(struct io_context *io, uint8_t cmd)
{
        int idx = _pio_spi_index(io->port_id);
        if(idx < 0)
                return;
        gpio_put(io->io.spi.pin_dc, false);
        gpio_put(io->io.spi.pin_cs, false);
        _pio_spi_write_blocking(_pio_spi_state[idx].pio, _pio_spi_state[idx].sm, &cmd, 1);
        gpio_put(io->io.spi.pin_cs, true);
}
void _platform_pio_spi4_send_data_byte(struct io_context *io, uint8_t data)
{
        int idx = _pio_spi_index(io->port_id);
        if(idx < 0)
                return;
        gpio_put(io->io.spi.pin_dc, true);
        gpio_put(io->io.spi.pin_cs, false);
        _pio_spi_write_blocking(_pio_spi_state[idx].pio, _pio_spi_state[idx].sm, &data, 1);
        gpio_put(io->io.spi.pin_cs, true);
}
// see _platform_spi4_send_data_burst() above -- same CS-held-for-the-whole-burst
// reasoning applies here
void _platform_pio_spi4_send_data_burst(struct io_context *io, const uint8_t *data, uint32_t len)
{
        int idx = _pio_spi_index(io->port_id);
        if(idx < 0)
                return;
        gpio_put(io->io.spi.pin_dc, true);
        gpio_put(io->io.spi.pin_cs, false);
        _pio_spi_write_blocking(_pio_spi_state[idx].pio, _pio_spi_state[idx].sm, data, len);
        gpio_put(io->io.spi.pin_cs, true);
}
