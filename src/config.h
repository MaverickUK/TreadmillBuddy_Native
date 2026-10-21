// Treadmill Buddy firmware: every tunable value, pins and build-time
// constants. The C++ counterpart of the CircuitPython settings.py: change
// these and the rest of the firmware adapts (plan length, number of bars...).
//
// Speeds are whole tenths of a km/h (25 = 2.5 km/h) so the plan maths is
// exact; times are milliseconds or seconds as named.
#pragma once

#include <stdint.h>
#include <stddef.h>

// Which Pico Display Pack this build is for: 28 (2.0"/2.8", 320x240) or
// 114 (the original 1.14", 240x135). Set per PlatformIO environment.
#ifndef DISPLAY_PACK
#define DISPLAY_PACK 28
#endif

namespace cfg {

// ---- Software identity (shown on the splash screen) ----
constexpr const char* APP_NAME = "Treadmill Buddy";
constexpr const char* APP_AUTHOR = "Peter Bridger";

// ---- Session plan ----
constexpr int      SESSION_DURATION_MIN      = 60;   // default session length
constexpr uint32_t SPEED_CHANGE_INTERVAL_S   = 300;  // one segment (bar) per this many seconds; 15 for bench tests
constexpr int      SPEED_STEP_TENTHS         = 5;    // speed goes up/down by this each segment (0.5 km/h)
constexpr int      MIN_SPEED_TENTHS          = 20;   // never below 2.0 km/h
constexpr int      MAX_SPEED_TENTHS          = 35;   // never above 3.5 km/h
constexpr int      START_SPEED_TENTHS        = 20;   // first segment of the plan
constexpr bool     RANDOM_PLAN               = true; // false: a fixed, repeatable zig-zag

// ---- Planning-screen adjustments (B picks the field, X raises, Y lowers) ----
// TIME: session length in TIME_STEP_MIN steps; the plan on screen is kept
// (extra segments continue the walk, removed ones come off the end).
// INTENSITY: each press moves exactly one segment by two speed steps, see plan.h.
constexpr int TIME_STEP_MIN            = 5;
constexpr int MIN_SESSION_DURATION_MIN = 5;
constexpr int MAX_SESSION_DURATION_MIN = 120;

// ---- Treadmill remote (STX882 315/433 MHz OOK transmitter on PIN_RF_TX) ----
// Captured button codes live in rf_codes.h.
constexpr uint8_t  PIN_RF_TX       = 27;   // STX882 DATA: HIGH keys the carrier
// Copies of a code sent back-to-back per press. The receiver acts once per
// decoded frame, so this behaves like how long the button is held: too few
// and a press is missed, too many and one tap fires twice. Tune it with the
// RF test mode (hold B at power-up): use the lowest value that never misses.
constexpr int      RF_REPEATS      = 6;
constexpr uint32_t BUTTON_GAP_MS   = 150;  // pause after each press

// How much one press of the remote's speed +/- changes the belt (0.1 km/h).
constexpr int      TREADMILL_SPEED_PER_PRESS_TENTHS = 1;
// The belt's speed straight after START, before any speed presses.
constexpr int      TREADMILL_START_SPEED_TENTHS     = 5;
// The treadmill's own countdown after START before the belt moves; speed
// presses are held back until it's over (they'd be ignored before).
constexpr uint32_t TREADMILL_START_DELAY_MS         = 5000;
// Pause presses STOP and resume presses START then ramps back up. false:
// leave the belt running while paused (only the session clock stops).
constexpr bool     PAUSE_STOPS_BELT = true;

// ---- Pico Display Pack LCD (ST7789 on SPI0; the same pins on every pack) ----
constexpr uint8_t  PIN_LCD_DC   = 16;
constexpr uint8_t  PIN_LCD_CS   = 17;
constexpr uint8_t  PIN_LCD_SCK  = 18;
constexpr uint8_t  PIN_LCD_MOSI = 19;
constexpr uint8_t  PIN_LCD_BL   = 20;   // backlight, HIGH = on
constexpr uint32_t LCD_SPI_HZ   = 62500000;  // clk_sys / 2 at 125 MHz: a full 320x240 frame in ~20 ms
constexpr bool     LCD_ROTATE_180 = false;   // flip if the picture is upside down for your mounting

#if DISPLAY_PACK == 28
constexpr int     DISPLAY_WIDTH  = 320;
constexpr int     DISPLAY_HEIGHT = 240;
constexpr int     LCD_COL_OFFSET = 0;
constexpr int     LCD_ROW_OFFSET = 0;
constexpr uint8_t PIN_LED_R = 26, PIN_LED_G = 27, PIN_LED_B = 28;
#elif DISPLAY_PACK == 114
constexpr int     DISPLAY_WIDTH  = 240;
constexpr int     DISPLAY_HEIGHT = 135;
constexpr int     LCD_COL_OFFSET = 40;
constexpr int     LCD_ROW_OFFSET = 53;
constexpr uint8_t PIN_LED_R = 6, PIN_LED_G = 7, PIN_LED_B = 8;
#else
#error "DISPLAY_PACK must be 28 or 114"
#endif

// ---- Display Pack buttons (active-low, internal pull-up) ----
constexpr uint8_t  PIN_BUTTON_A = 12;  // top-left:     start / pause / resume
constexpr uint8_t  PIN_BUTTON_B = 13;  // bottom-left:  planning: swap TIME / INTENSITY
constexpr uint8_t  PIN_BUTTON_X = 14;  // top-right:    planning: increase; running: stop
constexpr uint8_t  PIN_BUTTON_Y = 15;  // bottom-right: planning: decrease
constexpr uint32_t DEBOUNCE_MS  = 20;  // a press must be stable this long

// ---- On-board RGB LED (common anode) ----
// Off by default: on the 2.8" pack its green channel is GP27, the RF pin.
constexpr bool USE_RGB_LED          = false;
constexpr int  LED_BRIGHTNESS_PCT   = 50;

// ---- Screen timings ----
constexpr uint32_t SPLASH_MS           = 2000;
constexpr uint32_t COMPLETED_MS        = 30000;  // then back to a fresh plan
constexpr uint32_t SPEED_CHANGE_ALERT_MS = 4000; // the full-screen up/down triangle

// ---- Safety ----
constexpr uint32_t WATCHDOG_MS    = 2000;
constexpr uint32_t CORE1_STALL_MS = 1000;  // the treadmill core's heartbeat must advance within this

// ---- Derived (leave alone) ----
// The worst-case number of segments, which sizes the plan arrays.
constexpr int MAX_SEGMENTS = static_cast<int>(MAX_SESSION_DURATION_MIN * 60u / SPEED_CHANGE_INTERVAL_S);

static_assert(MAX_SEGMENTS >= 1, "SPEED_CHANGE_INTERVAL_S is longer than the longest session");
static_assert(MIN_SPEED_TENTHS <= START_SPEED_TENTHS && START_SPEED_TENTHS <= MAX_SPEED_TENTHS,
              "START_SPEED_TENTHS must be within MIN/MAX_SPEED_TENTHS");
static_assert(SPEED_STEP_TENTHS > 0 && (MAX_SPEED_TENTHS - MIN_SPEED_TENTHS) % SPEED_STEP_TENTHS == 0,
              "MIN..MAX_SPEED_TENTHS must be a whole number of SPEED_STEP_TENTHS");
static_assert(TREADMILL_SPEED_PER_PRESS_TENTHS > 0, "TREADMILL_SPEED_PER_PRESS_TENTHS must be positive");
static_assert(!USE_RGB_LED || (PIN_LED_R != PIN_RF_TX && PIN_LED_G != PIN_RF_TX && PIN_LED_B != PIN_RF_TX),
              "USE_RGB_LED: an LED channel shares PIN_RF_TX; move PIN_RF_TX or leave the LED off");

} // namespace cfg
