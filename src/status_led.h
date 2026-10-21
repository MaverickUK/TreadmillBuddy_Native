// The Display Pack's RGB LED (common anode, PWM), showing the session state.
// Off unless cfg::USE_RGB_LED. Core 0 only.
#pragma once

#include <stdint.h>

namespace status_led {

void begin();
// 0xRRGGBB, scaled by LED_BRIGHTNESS_PCT. Only writes the pins on a change.
void set(uint32_t rgb);

} // namespace status_led
