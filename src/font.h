// Anti-aliased proportional fonts, pre-rendered on the host by
// scripts/make_font.py into src/fonts/*.h. Pure C++, host-tested.
//
// Each glyph is a box of 4-bit coverage values (0 = none, 15 = solid), two
// pixels per byte with the left one in the high nibble, each row starting on
// a fresh byte. Canvas::text() blends them onto what's already drawn, so the
// edges come out smooth on any background. The arrays are const, so they
// stay in flash.
#pragma once

#include <stdint.h>

struct Glyph {
    uint32_t offset;  // into Font::bitmap
    uint8_t w, h;     // the inked box
    int8_t xOff;      // from the pen position to the box's left edge
    int8_t yOff;      // from the baseline to the box's top edge (negative = above)
    uint8_t advance;  // pen movement to the next glyph
};

struct Font {
    const uint8_t* bitmap;
    const Glyph* glyphs;
    char first, last;     // the characters covered
    uint8_t capHeight;    // height of a capital above the baseline (for centring)
    uint8_t lineHeight;   // baseline to baseline
};
