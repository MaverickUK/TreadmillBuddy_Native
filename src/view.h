// Everything a screen shows, as plain data. The app fills one in each pass;
// the main loop redraws only when it differs from the last one drawn, so the
// display is busy only when something visible changed. Pure C++.
#pragma once

#include <stdint.h>

#include "belt.h"
#include "plan.h"

enum class Screen : uint8_t { Splash, Planning, Session, SpeedChange, Completed, RfTest };
enum class Box : uint8_t { None, Starting, Resuming, Paused };  // the overlay on the chart screens

struct View {
    Screen screen = Screen::Splash;
    Box box = Box::None;

    // Splash
    bool recovered = false;  // booted from a watchdog reset (and stopped the belt)

    // Planning and session: the chart. `planVersion` changes whenever the
    // plan does, so views compare cheaply.
    const plan::Plan* plan = nullptr;
    uint32_t planVersion = 0;
    int durationMin = 0;
    int boostTenths = 0;
    bool editingTime = false;
    uint32_t distanceHkm = 0;  // hundredths of a km

    // Session (and the Starting/Resuming box)
    uint32_t elapsedS = 0;
    uint32_t remainingS = 0;
    uint16_t progress = 0;   // 0..PROGRESS_FULL across the session
    int speedTenths = 0;     // the big number: the belt's assumed speed
    bool paused = false;
    uint8_t countdownS = 0;  // Starting/Resuming: treadmill countdown left (0 = ramping)

    // Speed change: one big triangle in the colour of the new speed
    bool up = true;
    int alertTenths = 0;

    // Completed
    uint32_t leftS = 0;  // until a fresh plan

    // RF test mode
    belt::Press rfLast = belt::Press::None;
    bool rfSending = false;
    uint32_t rfPresses = 0;

    static constexpr uint16_t PROGRESS_FULL = 10000;

    bool operator==(const View& o) const {
        return screen == o.screen && box == o.box && recovered == o.recovered && planVersion == o.planVersion &&
               durationMin == o.durationMin && boostTenths == o.boostTenths && editingTime == o.editingTime &&
               distanceHkm == o.distanceHkm && elapsedS == o.elapsedS && remainingS == o.remainingS &&
               progress == o.progress && speedTenths == o.speedTenths && paused == o.paused &&
               countdownS == o.countdownS && up == o.up && alertTenths == o.alertTenths && leftS == o.leftS &&
               rfLast == o.rfLast && rfSending == o.rfSending && rfPresses == o.rfPresses;
    }
    bool operator!=(const View& o) const { return !(*this == o); }
};
