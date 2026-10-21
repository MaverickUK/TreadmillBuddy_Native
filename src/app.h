// The session state machine (the CircuitPython code.py). Pure C++ (no
// Arduino), host-tested: feed it the time, button presses and the belt's
// status; it says what the belt should do (target()) and what to show (view()).
//
// States: SPLASH -> PLANNING -> STARTING -> RUNNING <-> PAUSED, RUNNING ->
// COMPLETED -> PLANNING. STARTING waits for the treadmill core to report the
// belt counted down and ramped to speed; it's also used to resume from PAUSED
// when the pause stopped the belt. X (stop) from STARTING, RUNNING or PAUSED
// goes back to PLANNING.
//
// Buttons:
//   A (top-left)     = start / pause / resume
//   B (bottom-left)  = planning: swap between adjusting TIME and INTENSITY
//   X (top-right)    = planning: increase the selected field; otherwise stop
//   Y (bottom-right) = planning: decrease the selected field
#pragma once

#include <stdint.h>

#include "belt.h"
#include "config.h"
#include "plan.h"
#include "view.h"

struct Buttons {
    bool a = false, b = false, x = false, y = false;  // pressed since the last pass
    bool any() const { return a || b || x || y; }
};

class App {
public:
    enum class State : uint8_t { Splash, Planning, Starting, Running, Paused, Completed };

    struct Timing {
        uint32_t splashMs, completedMs, alertMs;
        bool pauseStopsBelt;
    };
    static constexpr Timing DEFAULT_TIMING{cfg::SPLASH_MS, cfg::COMPLETED_MS, cfg::SPEED_CHANGE_ALERT_MS,
                                           cfg::PAUSE_STOPS_BELT};

    App(const plan::Params& p, const Timing& t, uint32_t seed);
    App(const App&) = delete;  // planner_ refers to this App's rng_
    App& operator=(const App&) = delete;

    // Show the splash. `recovered`: booted from a watchdog reset.
    void begin(uint32_t nowMs, bool recovered);
    void update(uint32_t nowMs, const Buttons& b, const belt::Status& belt);

    const belt::Target& target() const { return target_; }
    State state() const { return state_; }
    View view(uint32_t nowMs, const belt::Status& belt) const;

    uint32_t elapsedMs() const { return elapsedMs_; }
    const plan::Plan& plan() const { return shown_; }
    const plan::Session& session() const { return session_; }
    static const char* stateName(State s);

private:
    void setBelt(bool running, int speedTenths);
    void toPlanning(bool freshPlan);
    void start();
    void updatePlanning(const Buttons& b);
    void updateRunning(uint32_t nowMs, const Buttons& b);
    int segmentFor(uint32_t elapsedMs) const;
    uint32_t totalMs() const { return planner_.totalS(session_) * 1000u; }
    void fillChart(View& v) const;

    plan::Rng rng_;
    plan::Planner planner_;
    Timing timing_;

    plan::Session session_;
    plan::Plan base_;   // the raw walk
    plan::Plan shown_;  // the walk with the intensity boost laid on top
    uint32_t planVersion_ = 1;
    bool editingTime_ = false;  // X/Y adjust SPEED first; B swaps to TIME

    State state_ = State::Splash;
    bool resuming_ = false;   // STARTING after a pause, not a new session
    bool recovered_ = false;
    uint32_t enteredMs_ = 0;  // when SPLASH / COMPLETED began

    uint32_t elapsedMs_ = 0;  // session time so far, excluding pauses
    uint32_t lastTickMs_ = 0;
    int appliedSeg_ = -1;     // the last segment whose speed went to the belt
    uint32_t alertUntilMs_ = 0;
    bool alertUp_ = true;
    int alertTenths_ = 0;

    belt::Target target_;
};
