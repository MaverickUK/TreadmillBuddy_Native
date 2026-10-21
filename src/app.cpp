#include "app.h"

App::App(const plan::Params& p, const Timing& t, uint32_t seed)
    : rng_(seed), planner_(p, rng_), timing_(t) {
    session_ = planner_.newSession();
    base_ = planner_.generate(session_);
    shown_ = planner_.applyBoost(base_, session_.boost);
}

const char* App::stateName(State s) {
    switch (s) {
        case State::Splash: return "SPLASH";
        case State::Planning: return "PLANNING";
        case State::Starting: return "STARTING";
        case State::Running: return "RUNNING";
        case State::Paused: return "PAUSED";
        case State::Completed: return "COMPLETED";
    }
    return "?";
}

void App::begin(uint32_t nowMs, bool recovered) {
    recovered_ = recovered;
    state_ = State::Splash;
    enteredMs_ = nowMs;
}

void App::setBelt(bool running, int speedTenths) {
    target_.running = running;
    target_.speedTenths = running ? speedTenths : 0;
    target_.gen = static_cast<uint16_t>(target_.gen + 1);
}

int App::segmentFor(uint32_t elapsedMs) const {
    const int seg = static_cast<int>(elapsedMs / (planner_.intervalS() * 1000u));
    return seg < shown_.n ? seg : shown_.n - 1;
}

// Back to the planning screen; `freshPlan` rolls a new walk (keeping the
// boost), as after a session or a stop.
void App::toPlanning(bool freshPlan) {
    if (freshPlan) {
        planner_.fresh(session_, base_, shown_);
        ++planVersion_;
    }
    appliedSeg_ = -1;
    alertUntilMs_ = 0;
    state_ = State::Planning;
}

void App::start() {
    elapsedMs_ = 0;
    appliedSeg_ = 0;
    alertUntilMs_ = 0;
    resuming_ = false;
    setBelt(true, shown_.speed[0]);
    state_ = State::Starting;
}

void App::update(uint32_t nowMs, const Buttons& b, const belt::Status& belt) {
    switch (state_) {
        case State::Splash:
            if (nowMs - enteredMs_ >= timing_.splashMs) state_ = State::Planning;
            break;

        case State::Planning:
            updatePlanning(b);
            break;

        case State::Starting:
            if (b.x) {  // stop: a cancelled start keeps its plan, a cancelled resume ends the session
                setBelt(false, 0);
                toPlanning(resuming_);
            } else if (belt.doneGen == target_.gen) {  // counted down and up to speed
                lastTickMs_ = nowMs;  // the session clock starts (or resumes) now
                state_ = State::Running;
            }
            break;

        case State::Running:
            updateRunning(nowMs, b);
            break;

        case State::Paused:
            if (b.a) {  // resume
                const int seg = segmentFor(elapsedMs_);
                if (timing_.pauseStopsBelt) {
                    setBelt(true, shown_.speed[seg]);
                    resuming_ = true;
                    state_ = State::Starting;
                } else {
                    lastTickMs_ = nowMs;  // ignore the time spent paused
                    state_ = State::Running;
                }
            } else if (b.x) {  // stop: new plan
                setBelt(false, 0);
                toPlanning(true);
            }
            break;

        case State::Completed:
            if (nowMs - enteredMs_ >= timing_.completedMs || b.any()) toPlanning(true);
            break;
    }
}

void App::updatePlanning(const Buttons& b) {
    if (b.a) {
        start();
    } else if (b.b) {  // swap which field X/Y change
        editingTime_ = !editingTime_;
    } else if (b.x || b.y) {  // adjust the selected field
        const int steps = b.x ? 1 : -1;
        if (editingTime_) {
            if (planner_.adjustTime(session_, steps)) {  // grow/trim the same walk
                planner_.resize(session_, base_, shown_);
                ++planVersion_;
            }
        } else if (planner_.adjustBoost(session_, steps, base_)) {
            shown_ = planner_.applyBoost(base_, session_.boost);
            ++planVersion_;
        }
    }
}

void App::updateRunning(uint32_t nowMs, const Buttons& b) {
    elapsedMs_ += nowMs - lastTickMs_;
    lastTickMs_ = nowMs;

    const int seg = segmentFor(elapsedMs_);
    if (seg != appliedSeg_) {  // crossed into a new segment
        const int speed = shown_.speed[seg];
        if (appliedSeg_ >= 0 && speed != shown_.speed[appliedSeg_]) {
            // the screen shows a big up/down triangle in the new speed's colour
            alertUp_ = speed > shown_.speed[appliedSeg_];
            alertTenths_ = speed;
            alertUntilMs_ = nowMs + timing_.alertMs;
        }
        // Unlike the CircuitPython version, the clock keeps running while the
        // belt ramps: the ramp no longer blocks, and it's part of the walk.
        setBelt(true, speed);
        appliedSeg_ = seg;
    }

    if (elapsedMs_ >= totalMs()) {
        elapsedMs_ = totalMs();
        setBelt(false, 0);
        alertUntilMs_ = 0;
        enteredMs_ = nowMs;
        state_ = State::Completed;
    } else if (b.a) {  // pause
        if (timing_.pauseStopsBelt) setBelt(false, 0);
        alertUntilMs_ = 0;
        state_ = State::Paused;
    } else if (b.x) {  // stop: new plan
        setBelt(false, 0);
        toPlanning(true);
    }
}

void App::fillChart(View& v) const {
    v.plan = &shown_;
    v.planVersion = planVersion_;
    v.durationMin = session_.durationMin;
    v.boostTenths = planner_.boostTenths(session_);
    v.editingTime = editingTime_;
    v.distanceHkm = planner_.distanceHundredthsKm(shown_);
    const uint32_t total = totalMs();
    v.elapsedS = elapsedMs_ / 1000;
    v.remainingS = (total - elapsedMs_) / 1000;
    v.progress = static_cast<uint16_t>(static_cast<uint64_t>(elapsedMs_) * View::PROGRESS_FULL / total);
}

View App::view(uint32_t nowMs, const belt::Status& belt) const {
    View v;
    switch (state_) {
        case State::Splash:
            v.screen = Screen::Splash;
            v.recovered = recovered_;
            break;

        case State::Planning:
            v.screen = Screen::Planning;
            fillChart(v);
            break;

        case State::Starting:
            fillChart(v);
            v.screen = resuming_ ? Screen::Session : Screen::Planning;
            v.box = resuming_ ? Box::Resuming : Box::Starting;
            v.speedTenths = belt.speedTenths;
            v.countdownS = static_cast<uint8_t>((belt.countdownLeftMs + 999) / 1000);
            break;

        case State::Running:
            fillChart(v);
            if (static_cast<int32_t>(nowMs - alertUntilMs_) < 0) {
                v.screen = Screen::SpeedChange;
                v.up = alertUp_;
                v.alertTenths = alertTenths_;
            } else {
                v.screen = Screen::Session;
                v.speedTenths = belt.speedTenths;
            }
            break;

        case State::Paused:
            fillChart(v);
            v.screen = Screen::Session;
            v.box = Box::Paused;
            v.paused = true;
            v.speedTenths = shown_.speed[segmentFor(elapsedMs_)];
            break;

        case State::Completed: {
            fillChart(v);
            v.screen = Screen::Completed;
            const uint32_t inState = nowMs - enteredMs_;
            v.leftS = inState < timing_.completedMs ? (timing_.completedMs - inState) / 1000 : 0;
            break;
        }
    }
    return v;
}
