#include "canvas.h"

#include <string.h>

#include "font5x7.h"

uint16_t Canvas::get(int x, int y) const {
    if (x < 0 || x >= w_ || y < 0 || y >= h_) return 0;
    return buf_[y * w_ + x];
}

void Canvas::fillRect(int x, int y, int w, int h, uint16_t c) {
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > w_) x1 = w_;
    if (y1 > h_) y1 = h_;
    for (int yy = y; yy < y1; ++yy) {
        uint16_t* row = buf_ + yy * w_;
        for (int xx = x; xx < x1; ++xx) row[xx] = c;
    }
}

// Scanline fill: for each row, the span between the triangle's edges.
void Canvas::fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
    // sort the corners by y
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y0 == y2) return;  // no height

    for (int y = y0; y <= y2; ++y) {
        // the long edge (0 -> 2), and whichever short edge covers this row
        int xa = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
        int xb;
        if (y < y1 || y1 == y2) xb = y1 == y0 ? x1 : x0 + (x1 - x0) * (y - y0) / (y1 - y0);
        else xb = x1 + (x2 - x1) * (y - y1) / (y2 - y1);
        if (xa > xb) { int t = xa; xa = xb; xb = t; }
        fillRect(xa, y, xb - xa + 1, 1, c);
    }
}

int Canvas::text(int x, int y, const char* s, int scale, uint16_t c) {
    for (; *s; ++s) {
        char ch = *s;
        if (ch < FONT5X7_FIRST || ch > FONT5X7_LAST) ch = '?';
        const uint8_t* glyph = &FONT5X7[(ch - FONT5X7_FIRST) * 5];
        for (int col = 0; col < 5; ++col)
            for (int row = 0; row < 7; ++row)
                if (glyph[col] & (1u << row)) fillRect(x + col * scale, y + row * scale, scale, scale, c);
        x += CHAR_W * scale;
    }
    return x;
}

void Canvas::text(int x, int y, const char* s, int scale, uint16_t c, Align h, Align v) {
    const int w = textWidth(s, scale), ht = textHeight(scale);
    if (h == Align::Middle) x -= w / 2;
    else if (h == Align::End) x -= w;
    if (v == Align::Middle) y -= ht / 2;
    else if (v == Align::End) y -= ht;
    text(x, y, s, scale, c);
}

int Canvas::textWidth(const char* s, int scale) {
    const int n = static_cast<int>(strlen(s));
    return n ? (n * CHAR_W - 1) * scale : 0;  // no trailing gap
}

void Canvas::blend(int x, int y, uint16_t c, uint8_t alpha) {
    if (x < 0 || x >= w_ || y < 0 || y >= h_ || alpha == 0) return;
    uint16_t& d = buf_[y * w_ + x];
    if (alpha >= 15) {
        d = c;
        return;
    }
    // per channel: (src * a + dst * (15 - a)) / 15
    const int inv = 15 - alpha;
    const int r = (((c >> 11) & 0x1F) * alpha + ((d >> 11) & 0x1F) * inv) / 15;
    const int g = (((c >> 5) & 0x3F) * alpha + ((d >> 5) & 0x3F) * inv) / 15;
    const int b = ((c & 0x1F) * alpha + (d & 0x1F) * inv) / 15;
    d = static_cast<uint16_t>(r << 11 | g << 5 | b);
}

int Canvas::text(const Font& font, int x, int baselineY, const char* s, uint16_t c) {
    for (; *s; ++s) {
        char ch = *s;
        if (ch < font.first || ch > font.last) ch = '?';
        const Glyph& g = font.glyphs[ch - font.first];
        const uint8_t* row = font.bitmap + g.offset;
        const int rowBytes = (g.w + 1) / 2;
        for (int yy = 0; yy < g.h; ++yy, row += rowBytes)
            for (int xx = 0; xx < g.w; ++xx) {
                const uint8_t pair = row[xx / 2];
                blend(x + g.xOff + xx, baselineY + g.yOff + yy, c, (xx & 1) ? (pair & 0x0F) : (pair >> 4));
            }
        x += g.advance;
    }
    return x;
}

void Canvas::text(const Font& font, int x, int y, const char* s, uint16_t c, Align h, Align v) {
    const int w = textWidth(font, s);
    if (h == Align::Middle) x -= w / 2;
    else if (h == Align::End) x -= w;
    // y -> baseline
    if (v == Align::Start) y += font.capHeight;
    else if (v == Align::Middle) y += font.capHeight / 2;
    text(font, x, y, s, c);
}

int Canvas::textWidth(const Font& font, const char* s) {
    int w = 0;
    for (; *s; ++s) {
        const char ch = (*s < font.first || *s > font.last) ? '?' : *s;
        w += font.glyphs[ch - font.first].advance;
    }
    return w;
}
