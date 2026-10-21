#include "status_led.h"

#include <Arduino.h>

#include "config.h"

namespace status_led {

namespace {

uint32_t shown = 0xFFFFFFFFu;

void write(uint8_t pin, uint32_t value) {
    const uint32_t level = value * cfg::LED_BRIGHTNESS_PCT / 100;
    analogWrite(pin, 255 - level);  // common anode: LOW = on
}

} // namespace

void begin() {
    if (!cfg::USE_RGB_LED) return;
    analogWriteFreq(1000);
    analogWriteRange(255);
    set(0);
}

void set(uint32_t rgb) {
    if (!cfg::USE_RGB_LED || rgb == shown) return;
    shown = rgb;
    write(cfg::PIN_LED_R, (rgb >> 16) & 0xFF);
    write(cfg::PIN_LED_G, (rgb >> 8) & 0xFF);
    write(cfg::PIN_LED_B, rgb & 0xFF);
}

} // namespace status_led
