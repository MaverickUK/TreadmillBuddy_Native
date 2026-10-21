// A 16-bit colour (RGB565) drawing surface over a caller-owned framebuffer,
// laid out as the ST7789 takes it: row by row, one uint16_t per pixel. Pure
// C++ (no Arduino), host-tested, and the host tests save it as images for
// previews.
//
// Text uses the 5x7 font in a 6x8 cell, scaled by whole pixels: the same
// blocky look as CircuitPython's terminalio labels. Large display text can
// use an anti-aliased Font (font.h) instead.
#pragma once

#include <stdint.h>

#include "font.h"

// 0xRRGGBB -> RGB565.
constexpr uint16_t rgb565(uint32_t rgb) {
    return static_cast<uint16_t>(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}

class Canvas {
public:
    static constexpr int CHAR_W = 6;  // 5 pixel glyph + 1 pixel gap
    static constexpr int CHAR_H = 8;

    // Where (x, y) sits on a piece of text, along each axis.
    enum class Align : uint8_t { Start, Middle, End };

    Canvas(uint16_t* buf, int width, int height) : buf_(buf), w_(width), h_(height) {}

    int width() const { return w_; }
    int height() const { return h_; }
    const uint16_t* data() const { return buf_; }
    uint16_t get(int x, int y) const;

    void fill(uint16_t c) { fillRect(0, 0, w_, h_, c); }
    // Filled rectangle; anything off the edges is clipped.
    void fillRect(int x, int y, int w, int h, uint16_t c);
    void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c);

    // Text with its top-left at (x, y). Returns the x after it.
    int text(int x, int y, const char* s, int scale, uint16_t c);
    // Text placed by an anchor point, like a displayio label's anchor_point.
    void text(int x, int y, const char* s, int scale, uint16_t c, Align h, Align v);
    // The inked size, without the trailing gap.
    static int textWidth(const char* s, int scale);
    static int textHeight(int scale) { return 7 * scale; }

    // Anti-aliased text in `font`, blended onto what's there, with its
    // baseline at y and pen starting at x. Returns the x after it.
    int text(const Font& font, int x, int baselineY, const char* s, uint16_t c);
    // Placed by an anchor: horizontally by the advance width, vertically by
    // the capital height (so Middle centres the capitals on y).
    void text(const Font& font, int x, int y, const char* s, uint16_t c, Align h, Align v);
    static int textWidth(const Font& font, const char* s);
    // Mix `c` over the pixel at (x, y) by coverage `alpha` (0-15).
    void blend(int x, int y, uint16_t c, uint8_t alpha);

private:
    uint16_t* buf_;
    int w_, h_;
};
