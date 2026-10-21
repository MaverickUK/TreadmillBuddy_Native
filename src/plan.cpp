#include "plan.h"

#include <string.h>

namespace plan {

namespace {

int clampTenths(int v, const Params& p) {
    if (v < p.minTenths) return p.minTenths;
    if (v > p.maxTenths) return p.maxTenths;
    return v;
}

// Python's `//`: rounds towards minus infinity, also for negative numbers.
int floorDiv(int a, int b) {
    const int q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

} // namespace

bool Plan::operator==(const Plan& o) const {
    return n == o.n && memcmp(speed, o.speed, n * sizeof(speed[0])) == 0;
}

Session Planner::newSession() const {
    Session s;
    s.durationMin = p_.defaultMin;
    return s;
}

int Planner::numSegments(const Session& s) const {
    const int n = static_cast<int>(s.durationMin * 60u / p_.intervalS);
    if (n < 1) return 1;
    return n > MAX_SEGMENTS ? MAX_SEGMENTS : n;
}

uint32_t Planner::totalS(const Session& s) const { return s.durationMin * 60u; }

int Planner::boostTenths(const Session& s) const {
    return (s.boostCarry + s.boost) * unitsPerPress(numSegments(s)) * p_.stepTenths;
}

bool Planner::adjustTime(Session& s, int steps) const {
    int want = s.durationMin + steps * p_.timeStepMin;
    if (want < p_.minSessionMin) want = p_.minSessionMin;
    else if (want > p_.maxSessionMin) want = p_.maxSessionMin;
    if (want == s.durationMin) return false;
    s.durationMin = want;
    return true;
}

bool Planner::adjustBoost(Session& s, int steps, const Plan& base) const {
    int low, high;
    boostLimits(base, low, high);
    int want = s.boost + steps;
    if (want < low) want = low;
    else if (want > high) want = high;
    if (want == s.boost) return false;
    s.boost = want;
    return true;
}

void Planner::clampBoost(Session& s, const Plan& base) const {
    int low, high;
    boostLimits(base, low, high);
    if (s.boost < low) s.boost = low;
    else if (s.boost > high) s.boost = high;
}

Plan Planner::generate(const Session& s) {
    Plan p;
    p.n = 1;
    p.speed[0] = static_cast<int16_t>(clampTenths(p_.startTenths, p_));
    extendWalk(p, numSegments(s));
    return p;
}

// Continue the walk in `p` (non-empty) until it has `n` segments.
void Planner::extendWalk(Plan& p, int n) {
    // the zig-zag carries on from wherever the walk last went
    bool goingUp = p.n < 2 || p.speed[p.n - 1] < p.speed[p.n - 2];

    while (p.n < n) {
        const int prev = p.speed[p.n - 1];
        int direction;
        if (prev <= p_.minTenths) {
            direction = +1;  // at the floor: must go up
        } else if (prev >= p_.maxTenths) {
            direction = -1;  // at the ceiling: must go down
        } else if (p_.random) {
            direction = rng_.coin() ? 1 : -1;
        } else {
            direction = goingUp ? 1 : -1;
            goingUp = !goingUp;
        }
        p.speed[p.n++] = static_cast<int16_t>(clampTenths(prev + direction * p_.stepTenths, p_));
    }
}

// The boost is locked in first: the shown plan becomes the new base and
// s.boost moves into s.boostCarry (less anything the dropped segments were
// carrying). Replaying the old presses on a longer or shorter base could
// otherwise pick different bars and reshape the part already seen.
void Planner::resize(Session& s, Plan& base, Plan& shown) {
    const int n = numSegments(s);
    const int keep = n < shown.n ? n : shown.n;
    if (keep) {
        int moved = 0;
        for (int i = 0; i < keep; ++i) moved += toUnit(shown.speed[i]) - toUnit(base.speed[i]);
        s.boostCarry += floorDiv(moved, unitsPerPress(shown.n));
    }
    s.boost = 0;

    if (!keep) {
        base = generate(s);
    } else {
        base = shown;
        base.n = keep;
        extendWalk(base, n);
    }
    shown = base;
}

// Any presses resize() locked in are turned back into live presses, since
// they belonged to the old walk.
void Planner::fresh(Session& s, Plan& base, Plan& shown) {
    base = generate(s);
    s.boost += s.boostCarry;
    s.boostCarry = 0;
    clampBoost(s, base);
    shown = applyBoost(base, s.boost);
}

// Move one segment for a single intensity press, in place (`units` are
// whole steps above minTenths).
//
// Every boundary must stay exactly one step, so a segment can only move if
// it's a valley (both neighbours one step above, going up) or a peak (both
// one step below, going down), and then by two steps, landing one step on
// the other side of its neighbours: the smallest legal change. The slowest
// valley (or fastest peak) goes first, earliest on a tie. A lone segment has
// no neighbours, so it just moves one step.
//
// Returns false when nothing can move (the plan has hit the speed limits).
bool Planner::boostOnce(int16_t* units, int n, bool up) const {
    const int top = topUnit();
    if (n == 1) {
        const int moved = units[0] + (up ? 1 : -1);
        if (moved < 0 || moved > top) return false;
        units[0] = static_cast<int16_t>(moved);
        return true;
    }

    const int delta = up ? 2 : -2;
    int pick = -1;
    for (int i = 0; i < n; ++i) {
        const int u = units[i];
        if (u + delta < 0 || u + delta > top) continue;
        // a valley/peak: every neighbour sits one step on the far side
        if (i > 0 && units[i - 1] != u + delta / 2) continue;
        if (i < n - 1 && units[i + 1] != u + delta / 2) continue;
        if (pick < 0 || (up && u < units[pick]) || (!up && u > units[pick])) pick = i;
    }
    if (pick < 0) return false;
    units[pick] = static_cast<int16_t>(units[pick] + delta);
    return true;
}

// Fills `units` with `base` after `steps` presses; returns how many presses
// actually applied.
int Planner::boostedUnits(const Plan& base, int steps, int16_t* units) const {
    for (int i = 0; i < base.n; ++i) units[i] = static_cast<int16_t>(toUnit(base.speed[i]));
    const int want = steps < 0 ? -steps : steps;
    int done = 0;
    while (done < want && boostOnce(units, base.n, steps > 0)) ++done;
    return done;
}

void Planner::boostLimits(const Plan& base, int& low, int& high) const {
    low = high = 0;
    if (base.n == 0) return;
    // each press moves a bar at least one step, so this bounds the search
    const int cap = base.n * (topUnit() + 1);
    int16_t units[MAX_SEGMENTS];
    low = -boostedUnits(base, -cap, units);
    high = boostedUnits(base, cap, units);
}

// Each press changes exactly one segment, by two steps (see boostOnce()). It's
// always replayed from the base plan, so backing off a press undoes exactly
// the bar that press moved.
Plan Planner::applyBoost(const Plan& base, int steps) const {
    if (base.n == 0) return base;
    int16_t units[MAX_SEGMENTS];
    boostedUnits(base, steps, units);
    Plan out;
    out.n = base.n;
    for (int i = 0; i < base.n; ++i) out.speed[i] = static_cast<int16_t>(p_.minTenths + units[i] * p_.stepTenths);
    return out;
}

uint32_t Planner::distanceHundredthsKm(const Plan& p) const {
    // km = sum(tenths) / 10 * intervalS / 3600, so hundredths = sum * intervalS / 360
    uint32_t sum = 0;
    for (int i = 0; i < p.n; ++i) sum += static_cast<uint32_t>(p.speed[i]);
    return (sum * p_.intervalS + 180) / 360;
}

} // namespace plan
