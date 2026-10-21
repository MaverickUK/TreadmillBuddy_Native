// Session plan generation. Pure C++ (no Arduino), host-tested against
// fixtures produced by the CircuitPython plan.py.
//
// A plan is a list of target speeds (tenths of km/h), one per
// SPEED_CHANGE_INTERVAL_S segment. There are two layers to it:
//   * a *base* plan: the random (or zig-zag) walk from generate();
//   * a *boost*: the intensity setting from the planning screen, in presses.
//     applyBoost() moves exactly one segment per press (the slowest valley
//     up, or the fastest peak down) by two speed steps: the smallest change
//     that keeps every segment exactly one step from its neighbours.
//
// Changing the session length keeps the plan as shown: resize() locks the
// boost into a new base, then appends fresh walk segments or drops ones off
// the end, so bars the user has already seen never move.
#pragma once

#include <stdint.h>

#include "config.h"

namespace plan {

struct Params {
    int minTenths, maxTenths, stepTenths, startTenths;
    uint32_t intervalS;
    bool random;
    int defaultMin, timeStepMin, minSessionMin, maxSessionMin;
};

constexpr Params DEFAULT_PARAMS{
    cfg::MIN_SPEED_TENTHS, cfg::MAX_SPEED_TENTHS, cfg::SPEED_STEP_TENTHS, cfg::START_SPEED_TENTHS,
    cfg::SPEED_CHANGE_INTERVAL_S, cfg::RANDOM_PLAN,
    cfg::SESSION_DURATION_MIN, cfg::TIME_STEP_MIN, cfg::MIN_SESSION_DURATION_MIN, cfg::MAX_SESSION_DURATION_MIN,
};

constexpr int MAX_SEGMENTS = cfg::MAX_SEGMENTS;

struct Plan {
    int n = 0;
    int16_t speed[MAX_SEGMENTS] = {};  // tenths of km/h

    bool operator==(const Plan& o) const;
    bool operator!=(const Plan& o) const { return !(*this == o); }
};

// A small, fast random source (xorshift32) for the walk's coin flips.
class Rng {
public:
    explicit Rng(uint32_t seed) : s_(seed ? seed : 0x9E3779B9u) {}
    bool coin() {
        s_ ^= s_ << 13;
        s_ ^= s_ >> 17;
        s_ ^= s_ << 5;
        return s_ & 0x80000000u;
    }

private:
    uint32_t s_;
};

// What the user can change on the planning screen (Python's SessionConfig).
struct Session {
    int durationMin = 0;
    int boost = 0;       // intensity presses, +/-
    int boostCarry = 0;  // presses already locked into the base by resize()
};

class Planner {
public:
    Planner(const Params& p, Rng& rng) : p_(p), rng_(rng) {}

    Session newSession() const;
    uint32_t intervalS() const { return p_.intervalS; }
    int numSegments(const Session& s) const;
    uint32_t totalS(const Session& s) const;
    // The speed the boost adds across the whole session (summed over
    // segments), in tenths of km/h: what the planning screen shows.
    int boostTenths(const Session& s) const;

    // Adjustments: true when something actually changed.
    bool adjustTime(Session& s, int steps) const;
    bool adjustBoost(Session& s, int steps, const Plan& base) const;
    void clampBoost(Session& s, const Plan& base) const;

    // A fresh base walk for `s`: starts at startTenths, then +/- one step per
    // segment within [min, max] (forced away from the limits).
    Plan generate(const Session& s);
    // Fit the shown plan to the session's (new) length without disturbing it.
    void resize(Session& s, Plan& base, Plan& shown);
    // A newly rolled walk, keeping the user's boost.
    void fresh(Session& s, Plan& base, Plan& shown);

    Plan applyBoost(const Plan& base, int steps) const;
    // The (min, max) presses `base` can take before every valley/peak is
    // pinned at the speed limits.
    void boostLimits(const Plan& base, int& low, int& high) const;

    // Total distance the plan covers, in hundredths of a km (rounded).
    uint32_t distanceHundredthsKm(const Plan& p) const;

private:
    void extendWalk(Plan& p, int n);
    int toUnit(int tenths) const { return (tenths - p_.minTenths) / p_.stepTenths; }
    int topUnit() const { return (p_.maxTenths - p_.minTenths) / p_.stepTenths; }
    static int unitsPerPress(int numSegments) { return numSegments > 1 ? 2 : 1; }
    bool boostOnce(int16_t* units, int n, bool up) const;
    int boostedUnits(const Plan& base, int steps, int16_t* units) const;

    Params p_;
    Rng& rng_;
};

} // namespace plan
