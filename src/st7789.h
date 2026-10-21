// The Pico Display Pack's ST7789 LCD on SPI0. Core 0 only.
//
// Frames go out by DMA: pushFrame() starts the transfer and returns at once,
// so the CPU is free while the ~20 ms of pixels shift out. Don't touch the
// framebuffer until busy() says the transfer is over.
#pragma once

#include <stdint.h>

namespace lcd {

// Backlight off first (the panel's RAM holds noise from power-up), then the
// panel init, one black frame, then the backlight on.
void begin(uint16_t* framebuffer);

// True while a frame is still going out.
bool busy();

// Send the whole framebuffer (DISPLAY_WIDTH x DISPLAY_HEIGHT RGB565). Call
// only when !busy().
void pushFrame(const uint16_t* framebuffer);

// How long the last frame's transfer took (µs), for the serial stats.
uint32_t lastFrameUs();

} // namespace lcd
