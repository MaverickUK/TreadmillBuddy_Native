#include "treadmill.h"

#include <Arduino.h>

#include "config.h"
#include "rf_codes.h"
#include "rf_tx.h"

namespace treadmill {

TreadmillLink link;

namespace {

constexpr belt::Params PARAMS{cfg::TREADMILL_SPEED_PER_PRESS_TENTHS, cfg::TREADMILL_START_SPEED_TENTHS,
                              cfg::TREADMILL_START_DELAY_MS};

belt::State state;  // the assumed belt state; core 1 only
uint16_t doneGen = 0;

void beat() { link.beat(); }

// Sleep in short slices so the heartbeat keeps going.
void wait(uint32_t ms) {
    const uint32_t start = millis();
    while (millis() - start < ms) {
        beat();
        delay(5);
    }
}

template <size_t N>
void sendCode(const uint16_t (&code)[N]) {
    rf::send(rf_codes::LEAD_SYNC, sizeof(rf_codes::LEAD_SYNC) / sizeof(rf_codes::LEAD_SYNC[0]), code, N,
             cfg::RF_REPEATS, beat);
}

// One press of a remote button, then the gap before the next.
void press(belt::Press p) {
    switch (p) {
        case belt::Press::Start: sendCode(rf_codes::START); break;
        case belt::Press::Stop: sendCode(rf_codes::STOP); break;
        case belt::Press::Up: sendCode(rf_codes::SPEED_UP); break;
        case belt::Press::Down: sendCode(rf_codes::SPEED_DOWN); break;
        case belt::Press::None: return;
    }
    link.countPress();
}

} // namespace

void core1Setup() {
    rf::begin(cfg::PIN_RF_TX);
    link.publish(state, doneGen, millis());
}

void core1Loop() {
    beat();

    // A raw press (RF test mode, serial console) goes out as-is and doesn't
    // change the tracked belt state.
    const belt::Press raw = link.takeRaw();
    if (raw != belt::Press::None) {
        press(raw);
        link.rawSent();
        wait(cfg::BUTTON_GAP_MS);
        return;
    }

    // Re-read the target before every press, so a newer one (a stop, most
    // urgently) takes over straight away.
    const belt::Target target = link.target();
    const uint32_t now = millis();
    const belt::Press next = belt::next(state, target, now, PARAMS);
    if (next != belt::Press::None) {
        press(next);
        belt::apply(state, next, target, millis(), PARAMS);
        link.publish(state, doneGen, millis());
        wait(cfg::BUTTON_GAP_MS);
        return;
    }

    belt::apply(state, belt::Press::None, target, now, PARAMS);  // settles a part-press difference
    if (belt::reached(state, target, now, PARAMS)) doneGen = target.gen;
    link.publish(state, doneGen, now);
    delay(2);
}

} // namespace treadmill
