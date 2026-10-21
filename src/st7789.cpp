#include "st7789.h"

#include <Arduino.h>
#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/spi.h>

#include <initializer_list>

#include "config.h"

namespace lcd {

namespace {

spi_inst_t* const SPI = spi0;  // GP18/19 are SPI0's SCK/TX
int dmaChannel = -1;
bool sending = false;
uint32_t frameStartUs = 0;
uint32_t frameUs = 0;

// ST7789 commands
constexpr uint8_t SWRESET = 0x01, SLPOUT = 0x11, INVON = 0x21, DISPON = 0x29, CASET = 0x2A, RASET = 0x2B,
                  RAMWR = 0x2C, TEON = 0x35, MADCTL = 0x36, COLMOD = 0x3A, PORCTRL = 0xB2, GCTRL = 0xB7,
                  VCOMS = 0xBB, LCMCTRL = 0xC0, VDVVRHEN = 0xC2, VRHS = 0xC3, VDVS = 0xC4, FRCTRL2 = 0xC6,
                  PWCTRL1 = 0xD0, GMCTRP1 = 0xE0, GMCTRN1 = 0xE1;
// MADCTL bits
constexpr uint8_t ROW_ORDER = 0x80, COL_ORDER = 0x40, SWAP_XY = 0x20, SCAN_ORDER = 0x10;

void command(uint8_t cmd, const uint8_t* data = nullptr, size_t n = 0) {
    gpio_put(cfg::PIN_LCD_CS, 0);
    gpio_put(cfg::PIN_LCD_DC, 0);
    spi_write_blocking(SPI, &cmd, 1);
    if (n) {
        gpio_put(cfg::PIN_LCD_DC, 1);
        spi_write_blocking(SPI, data, n);
    }
    gpio_put(cfg::PIN_LCD_CS, 1);
}

void command(uint8_t cmd, std::initializer_list<uint8_t> data) { command(cmd, data.begin(), data.size()); }

void window(int x, int y, int w, int h) {
    const int x0 = x + cfg::LCD_COL_OFFSET, x1 = x0 + w - 1;
    const int y0 = y + cfg::LCD_ROW_OFFSET, y1 = y0 + h - 1;
    command(CASET, {uint8_t(x0 >> 8), uint8_t(x0), uint8_t(x1 >> 8), uint8_t(x1)});
    command(RASET, {uint8_t(y0 >> 8), uint8_t(y0), uint8_t(y1 >> 8), uint8_t(y1)});
}

// The init sequence and voltages are Pimoroni's for these packs
// (pimoroni-pico drivers/st7789), as are the window offsets in config.h.
void initPanel() {
    command(SWRESET);
    delay(150);
    command(TEON);
    command(COLMOD, {0x05});  // 16 bits per pixel
    command(PORCTRL, {0x0C, 0x0C, 0x00, 0x33, 0x33});
    command(LCMCTRL, {0x2C});
    command(VDVVRHEN, {0x01});
    command(VRHS, {0x12});
    command(VDVS, {0x20});
    command(PWCTRL1, {0xA4, 0xA1});
    command(FRCTRL2, {0x0F});
#if DISPLAY_PACK == 28
    command(GCTRL, {0x35});
    command(VCOMS, {0x1F});
    command(0xD6, {0xA1});
    command(GMCTRP1, {0xD0, 0x08, 0x11, 0x08, 0x0C, 0x15, 0x39, 0x33, 0x50, 0x36, 0x13, 0x14, 0x29, 0x2D});
    command(GMCTRN1, {0xD0, 0x08, 0x10, 0x08, 0x06, 0x06, 0x39, 0x44, 0x51, 0x0B, 0x16, 0x14, 0x2F, 0x31});
#else
    command(VRHS, {0x00});
    command(GCTRL, {0x75});
    command(VCOMS, {0x3D});
    command(0xD6, {0xA1});
    command(GMCTRP1, {0x70, 0x04, 0x08, 0x09, 0x09, 0x05, 0x2A, 0x33, 0x41, 0x07, 0x13, 0x13, 0x29, 0x2F});
    command(GMCTRN1, {0x70, 0x03, 0x09, 0x0A, 0x09, 0x06, 0x2B, 0x34, 0x41, 0x07, 0x12, 0x14, 0x28, 0x2E});
#endif
    command(INVON);
    command(SLPOUT);
    command(DISPON);
    delay(100);
    // landscape, with the Display Pack's buttons either side
    command(MADCTL, {uint8_t((cfg::LCD_ROTATE_180 ? ROW_ORDER : COL_ORDER) | SWAP_XY | SCAN_ORDER)});
}

// Once the DMA has handed the last pixel to the SPI, wait for it to shift
// out, then end the transfer.
void finish() {
    while (spi_is_busy(SPI)) tight_loop_contents();
    gpio_put(cfg::PIN_LCD_CS, 1);
    spi_set_format(SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    sending = false;
    frameUs = micros() - frameStartUs;
}

} // namespace

void begin(uint16_t* framebuffer) {
    gpio_init(cfg::PIN_LCD_BL);
    gpio_set_dir(cfg::PIN_LCD_BL, GPIO_OUT);
    gpio_put(cfg::PIN_LCD_BL, 0);
    for (uint8_t pin : {cfg::PIN_LCD_CS, cfg::PIN_LCD_DC}) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_put(pin, 1);
    }

    spi_init(SPI, cfg::LCD_SPI_HZ);
    gpio_set_function(cfg::PIN_LCD_SCK, GPIO_FUNC_SPI);
    gpio_set_function(cfg::PIN_LCD_MOSI, GPIO_FUNC_SPI);

    dmaChannel = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(dmaChannel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_dreq(&c, spi_get_dreq(SPI, true));
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    dma_channel_configure(dmaChannel, &c, &spi_get_hw(SPI)->dr, nullptr, 0, false);

    initPanel();

    for (int i = 0; i < cfg::DISPLAY_WIDTH * cfg::DISPLAY_HEIGHT; ++i) framebuffer[i] = 0;
    pushFrame(framebuffer);
    while (busy()) tight_loop_contents();
    delay(20);
    gpio_put(cfg::PIN_LCD_BL, 1);
}

bool busy() {
    if (!sending) return false;
    if (dma_channel_is_busy(dmaChannel)) return true;
    finish();
    return false;
}

void pushFrame(const uint16_t* framebuffer) {
    window(0, 0, cfg::DISPLAY_WIDTH, cfg::DISPLAY_HEIGHT);
    gpio_put(cfg::PIN_LCD_CS, 0);
    gpio_put(cfg::PIN_LCD_DC, 0);
    const uint8_t cmd = RAMWR;
    spi_write_blocking(SPI, &cmd, 1);
    gpio_put(cfg::PIN_LCD_DC, 1);
    // 16-bit SPI frames send each pixel high byte first, as the panel wants
    spi_set_format(SPI, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    sending = true;
    frameStartUs = micros();
    dma_channel_transfer_from_buffer_now(dmaChannel, framebuffer, cfg::DISPLAY_WIDTH * cfg::DISPLAY_HEIGHT);
}

uint32_t lastFrameUs() { return frameUs; }

} // namespace lcd
