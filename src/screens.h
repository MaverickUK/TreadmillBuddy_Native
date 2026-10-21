// Drawing each screen onto a Canvas from a View. Pure C++ (no Arduino);
// the host tests render these to images (docs/screens).
//
// A port of the CircuitPython ui.py: the same layout, colours and text
// scales, worked out from the canvas size. Panels under 160 pixels tall (the
// 1.14" pack) get the compact profile, taller ones (2.0"/2.8") the roomy one.
#pragma once

#include <stdint.h>

#include "canvas.h"
#include "view.h"

namespace screens {

// Slow to fast as green, yellow, orange, red (0xRRGGBB), across the
// MIN..MAX_SPEED_TENTHS range.
uint32_t speedColour(int tenths);

// Redraw the whole screen.
void draw(Canvas& c, const View& v);

} // namespace screens
