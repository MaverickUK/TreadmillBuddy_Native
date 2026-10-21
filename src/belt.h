// The treadmill's assumed state, and which remote button to press next to
// reach a target. Pure C++ (no Arduino), host-tested.
//
// The controller is open loop: it has no feedback from the treadmill, so it
// tracks an *assumed* belt state from the presses it has sent. Core 0 only
// says what it wants (a Target: running or not, at what speed); core 1
// presses buttons one at a time until the assumed state matches, re-reading
// the target between presses. So a newer target always wins, and a stop
// interrupts a speed ramp or the START countdown straight away.
#pragma once

#include <stdint.h>

namespace belt {

// What core 0 wants. `gen` changes with every new target, so core 1 can say
// which target it has reached.
struct Target {
    bool running = false;
    int speedTenths = 0;
    uint16_t gen = 0;
};

// What core 1 believes the belt is doing.
struct State {
    bool running = false;
    int speedTenths = 0;
    uint32_t countdownEndMs = 0;  // while running and now < this, the belt is still counting down
    bool counting = false;
};

// What core 0 sees of it.
struct Status {
    bool running = false;
    int speedTenths = 0;
    uint32_t countdownLeftMs = 0;  // > 0 while the treadmill counts down after START
    uint16_t doneGen = 0;          // the latest target gen the belt has reached
};

enum class Press : uint8_t { None, Start, Stop, Up, Down };

struct Params {
    int stepTenths;        // belt change per speed press
    int startSpeedTenths;  // belt speed straight after START
    uint32_t startDelayMs; // treadmill's countdown after START
};

// The next press needed to move `s` towards `t`, or None if it's there
// already or the START countdown is still running.
Press next(const State& s, const Target& t, uint32_t nowMs, const Params& p);

// True when `s` matches `t` (nothing left to press and no countdown).
bool reached(const State& s, const Target& t, uint32_t nowMs, const Params& p);

// Update the assumed state after sending `press` at `nowMs`.
void apply(State& s, Press press, const Target& t, uint32_t nowMs, const Params& p);

} // namespace belt
