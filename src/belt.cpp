#include "belt.h"

namespace belt {

namespace {

bool counting(const State& s, uint32_t nowMs) {
    return s.running && s.counting && static_cast<int32_t>(nowMs - s.countdownEndMs) < 0;
}

// Presses needed to cover `diff`, rounded like the Python set_speed().
int pressesFor(int diff, const Params& p) {
    const int mag = diff < 0 ? -diff : diff;
    return (mag + p.stepTenths / 2) / p.stepTenths;
}

} // namespace

Press next(const State& s, const Target& t, uint32_t nowMs, const Params& p) {
    if (!t.running) return s.running ? Press::Stop : Press::None;
    if (!s.running) return Press::Start;
    if (counting(s, nowMs)) return Press::None;  // presses now would be ignored
    const int diff = t.speedTenths - s.speedTenths;
    if (pressesFor(diff, p) == 0) return Press::None;
    return diff > 0 ? Press::Up : Press::Down;
}

bool reached(const State& s, const Target& t, uint32_t nowMs, const Params& p) {
    return next(s, t, nowMs, p) == Press::None && !counting(s, nowMs);
}

void apply(State& s, Press press, const Target& t, uint32_t nowMs, const Params& p) {
    switch (press) {
        case Press::None:
            // Within half a press of the target counts as there (Python
            // set_speed() also took the target as the new speed).
            if (s.running && t.running && reached(s, t, nowMs, p)) s.speedTenths = t.speedTenths;
            break;
        case Press::Start:
            s.running = true;
            s.speedTenths = p.startSpeedTenths;
            s.countdownEndMs = nowMs + p.startDelayMs;
            s.counting = true;
            break;
        case Press::Stop:
            s.running = false;
            s.speedTenths = 0;
            s.counting = false;
            break;
        case Press::Up:
        case Press::Down: {
            const int step = press == Press::Up ? p.stepTenths : -p.stepTenths;
            s.speedTenths += step;
            // never overshoot: a last part-step lands on the target
            if ((step > 0 && s.speedTenths > t.speedTenths) || (step < 0 && s.speedTenths < t.speedTenths))
                s.speedTenths = t.speedTenths;
            break;
        }
    }
    if (!counting(s, nowMs)) s.counting = false;
}

} // namespace belt
