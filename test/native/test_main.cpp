// Host-side unit tests for the platform-independent firmware code: the plan
// (checked against fixtures from the CircuitPython plan.py), the belt
// reconciler and the link between the cores (including a two-thread test
// that mirrors core 0 / core 1), the session state machine driven through
// whole sessions against a simulated treadmill, buttons, the canvas and the
// screens (optionally saved as previews).
//
// Run: test/run_native_tests.sh
// Previews: TREADMILL_PREVIEW_DIR=docs/screens test/run_native_tests.sh && python3 test/ppm_to_png.py docs/screens
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "app.h"
#include "belt.h"
#include "button.h"
#include "canvas.h"
#include "fonts/poppins_bold_32.h"
#include "fonts/poppins_bold_56.h"
#include "plan.h"
#include "plan_fixtures.h"
#include "screens.h"
#include "treadmill_link.h"
#include "view.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++checks;                                                                \
        if (!(cond)) {                                                           \
            ++failures;                                                          \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
        }                                                                        \
    } while (0)
#define CHECK_EQ(a, b)                                                           \
    do {                                                                         \
        ++checks;                                                                \
        auto _a = (a);                                                           \
        auto _b = (b);                                                           \
        if (!(_a == _b)) {                                                       \
            ++failures;                                                          \
            std::printf("  FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, \
                        #a, #b, (long long)_a, (long long)_b);                   \
        }                                                                        \
    } while (0)

#define TEST(name) static void name()
#define RUN(name)                                     \
    do {                                              \
        std::printf("%s\n", #name);                   \
        name();                                       \
    } while (0)

// ---------------------------------------------------------------------------
// Plan: parity with the CircuitPython plan.py
// ---------------------------------------------------------------------------
static plan::Params pyParams(bool random) {
    using namespace fixtures;
    return plan::Params{MIN_TENTHS, MAX_TENTHS, STEP_TENTHS, START_TENTHS, static_cast<uint32_t>(INTERVAL_S),
                        random, DEFAULT_MIN, TIME_STEP_MIN, MIN_SESSION_MIN, MAX_SESSION_MIN};
}
static_assert(fixtures::MAX_SESSION_MIN * 60 / fixtures::INTERVAL_S <= plan::MAX_SEGMENTS,
              "the fixtures' longest plan must fit plan::MAX_SEGMENTS");

struct Reader {
    const int* p;
    int next() { return *p++; }
    plan::Plan plan(int n) {
        plan::Plan out;
        out.n = n;
        for (int i = 0; i < n; ++i) out.speed[i] = static_cast<int16_t>(next());
        return out;
    }
};

static bool samePlan(const plan::Plan& a, const plan::Plan& b, const char* what, int caseNo) {
    if (a == b) return true;
    std::printf("  case %d %s differs: got", caseNo, what);
    for (int i = 0; i < a.n; ++i) std::printf(" %d", a.speed[i]);
    std::printf(" | python");
    for (int i = 0; i < b.n; ++i) std::printf(" %d", b.speed[i]);
    std::printf("\n");
    return false;
}

TEST(plan_boost_matches_python) {
    plan::Rng rng(1);
    const plan::Params params = pyParams(true);
    plan::Planner planner(params, rng);
    Reader r{fixtures::BOOST};
    const int count = r.next();
    for (int c = 0; c < count; ++c) {
        const int n = r.next();
        const plan::Plan base = r.plan(n);
        const int low = r.next(), high = r.next(), smin = r.next(), smax = r.next();
        int gotLow, gotHigh;
        planner.boostLimits(base, gotLow, gotHigh);
        CHECK_EQ(gotLow, low);
        CHECK_EQ(gotHigh, high);
        for (int s = smin; s <= smax; ++s) CHECK(samePlan(planner.applyBoost(base, s), r.plan(n), "boost", c));
    }
}

TEST(plan_zigzag_matches_python) {
    plan::Rng rng(1);
    const plan::Params params = pyParams(false);
    plan::Planner planner(params, rng);
    Reader r{fixtures::ZIGZAG};
    const int count = r.next();
    CHECK_EQ(count, 24);
    for (int c = 0; c < count; ++c) {
        plan::Session s = planner.newSession();
        s.durationMin = r.next();
        const int n = r.next();
        CHECK_EQ(planner.numSegments(s), n);
        CHECK(samePlan(planner.generate(s), r.plan(n), "zigzag", c));
    }
}

// The planning screen: X/Y on INTENSITY and TIME, replayed as code.py does
// (and app.cpp's updatePlanning()), then a fresh plan.
TEST(plan_planning_ops_match_python) {
    Reader r{fixtures::OPS};
    const int count = r.next();
    CHECK_EQ(count, 80);
    for (int c = 0; c < count; ++c) {
        const bool random = r.next();
        plan::Rng rng(c + 1);
        const plan::Params params = pyParams(random);
        plan::Planner planner(params, rng);
        plan::Session s = planner.newSession();
        s.durationMin = r.next();
        plan::Plan base = r.plan(r.next());
        plan::Plan shown = planner.applyBoost(base, s.boost);

        const int nops = r.next();
        for (int i = 0; i < nops; ++i) {
            const int op = r.next();
            const int steps = op > 0 ? 1 : -1;
            if (op == 2 || op == -2) {
                if (planner.adjustTime(s, steps)) planner.resize(s, base, shown);
            } else if (planner.adjustBoost(s, steps, base)) {
                shown = planner.applyBoost(base, s.boost);
            }
        }

        CHECK_EQ(s.durationMin, r.next());
        const int n = r.next();
        CHECK(samePlan(base, r.plan(n), "base", c));
        CHECK(samePlan(shown, r.plan(n), "shown", c));
        CHECK_EQ(s.boost, r.next());
        CHECK_EQ(s.boostCarry, r.next());
        CHECK_EQ(planner.boostTenths(s), r.next());
        const int km = r.next();  // Python rounds a float: allow a hundredth either way
        const int got = static_cast<int>(planner.distanceHundredthsKm(shown));
        CHECK(got >= km - 1 && got <= km + 1);

        if (!random) {
            planner.fresh(s, base, shown);
            CHECK_EQ(s.boost, r.next());
            CHECK_EQ(s.boostCarry, r.next());
            CHECK(samePlan(shown, r.plan(r.next()), "fresh", c));
        }
    }
}

TEST(plan_random_walk_rules) {
    const plan::Params params = pyParams(true);
    bool sawUp = false, sawDown = false;
    for (uint32_t seed = 1; seed <= 50; ++seed) {
        plan::Rng rng(seed);
        plan::Planner planner(params, rng);
        plan::Session s = planner.newSession();
        s.durationMin = 120;
        const plan::Plan p = planner.generate(s);
        CHECK_EQ(p.n, 24);
        CHECK_EQ(p.speed[0], params.startTenths);
        for (int i = 1; i < p.n; ++i) {
            const int d = p.speed[i] - p.speed[i - 1];
            CHECK(d == params.stepTenths || d == -params.stepTenths);
            CHECK(p.speed[i] >= params.minTenths && p.speed[i] <= params.maxTenths);
            if (i > 1 && p.speed[i - 1] > params.minTenths && p.speed[i - 1] < params.maxTenths) {
                (d > 0 ? sawUp : sawDown) = true;  // a free choice went this way
            }
        }
    }
    CHECK(sawUp);
    CHECK(sawDown);
}

TEST(plan_distance) {
    plan::Rng rng(1);
    const plan::Params params = pyParams(false);
    plan::Planner planner(params, rng);
    plan::Plan p;
    p.n = 12;
    for (int i = 0; i < p.n; ++i) p.speed[i] = 30;  // an hour at 3.0 km/h
    CHECK_EQ(planner.distanceHundredthsKm(p), 300u);
    p.n = 1;
    p.speed[0] = 25;  // 5 minutes at 2.5 km/h = 0.2083 km
    CHECK_EQ(planner.distanceHundredthsKm(p), 21u);
}

// ---------------------------------------------------------------------------
// Belt reconciler
// ---------------------------------------------------------------------------
static const belt::Params BELT{1, 5, 5000};  // 0.1 km/h a press, 0.5 after START, 5 s countdown

TEST(belt_start_counts_down_then_ramps) {
    belt::State s;
    belt::Target t{true, 20, 1};
    CHECK(belt::next(s, t, 0, BELT) == belt::Press::Start);
    belt::apply(s, belt::Press::Start, t, 0, BELT);
    CHECK(s.running);
    CHECK_EQ(s.speedTenths, 5);
    CHECK(belt::next(s, t, 4999, BELT) == belt::Press::None);  // still counting down
    CHECK(!belt::reached(s, t, 4999, BELT));
    int presses = 0;
    uint32_t now = 5000;
    for (belt::Press p; (p = belt::next(s, t, now, BELT)) != belt::Press::None; now += 400) {
        CHECK(p == belt::Press::Up);
        belt::apply(s, p, t, now, BELT);
        ++presses;
    }
    CHECK_EQ(presses, 15);  // 0.5 -> 2.0 km/h
    CHECK_EQ(s.speedTenths, 20);
    CHECK(belt::reached(s, t, now, BELT));

    t = {true, 15, 2};
    for (presses = 0; belt::next(s, t, now, BELT) == belt::Press::Down; ++presses) belt::apply(s, belt::Press::Down, t, now, BELT);
    CHECK_EQ(presses, 5);

    t = {false, 0, 3};
    CHECK(belt::next(s, t, now, BELT) == belt::Press::Stop);
    belt::apply(s, belt::Press::Stop, t, now, BELT);
    CHECK(!s.running);
    CHECK(belt::reached(s, t, now, BELT));
    CHECK(belt::next(s, t, now, BELT) == belt::Press::None);  // never presses STOP twice
}

TEST(belt_stop_interrupts_the_countdown) {
    belt::State s;
    belt::Target t{true, 30, 1};
    belt::apply(s, belt::Press::Start, t, 0, BELT);
    t = {false, 0, 2};
    CHECK(belt::next(s, t, 1000, BELT) == belt::Press::Stop);
}

TEST(belt_part_presses_round_like_python) {
    const belt::Params coarse{2, 5, 0};  // 0.2 km/h a press
    belt::State s;
    s.running = true;
    s.speedTenths = 20;
    belt::Target t{true, 21, 1};
    CHECK(belt::next(s, t, 0, coarse) == belt::Press::Up);  // 0.1 rounds to one press
    belt::apply(s, belt::Press::Up, t, 0, coarse);
    CHECK_EQ(s.speedTenths, 21);  // and lands on the target, not past it

    const belt::Params wide{3, 5, 0};  // 0.3 km/h a press: 0.1 rounds to none
    s.speedTenths = 20;
    CHECK(belt::next(s, t, 0, wide) == belt::Press::None);
    belt::apply(s, belt::Press::None, t, 0, wide);
    CHECK_EQ(s.speedTenths, 21);  // taken as there, like Python's set_speed()
}

// ---------------------------------------------------------------------------
// The link between the cores
// ---------------------------------------------------------------------------
TEST(link_round_trip) {
    TreadmillLink link;
    link.setTarget({true, 35, 65535});
    const belt::Target t = link.target();
    CHECK(t.running);
    CHECK_EQ(t.speedTenths, 35);
    CHECK_EQ(t.gen, 65535);

    belt::State s;
    s.running = true;
    s.speedTenths = 5;
    s.counting = true;
    s.countdownEndMs = 0xFFFFFF00u + 3000;  // wraps past 0
    link.publish(s, 7, 0xFFFFFF00u);
    belt::Status st = link.status(0xFFFFFF00u + 1000);
    CHECK(st.running);
    CHECK_EQ(st.speedTenths, 5);
    CHECK_EQ(st.doneGen, 7);
    CHECK_EQ(st.countdownLeftMs, 2000u);
    link.publish(s, 7, 0xFFFFFF00u + 3000);  // countdown over
    CHECK_EQ(link.status(0xFFFFFF00u + 3000).countdownLeftMs, 0u);
}

TEST(link_raw_presses) {
    TreadmillLink link;
    CHECK(link.takeRaw() == belt::Press::None);
    CHECK(!link.rawPending());
    link.requestRaw(belt::Press::Up);
    link.requestRaw(belt::Press::Stop);  // replaces the one not yet sent
    CHECK(link.rawPending());
    CHECK(link.takeRaw() == belt::Press::Stop);
    CHECK(link.rawPending());  // until it's out
    link.rawSent();
    CHECK(!link.rawPending());
    CHECK(link.takeRaw() == belt::Press::None);
}

// Core 0 changes its mind quickly; core 1 always ends at the latest target.
TEST(link_two_threads_latest_target_wins) {
    TreadmillLink link;
    std::atomic<bool> stop{false};
    std::thread core1([&] {
        belt::State s;
        uint16_t done = 0;
        const belt::Params p{1, 5, 0};
        uint32_t now = 0;
        while (!stop.load()) {
            const belt::Target t = link.target();
            const belt::Press next = belt::next(s, t, now, p);
            belt::apply(s, next, t, now, p);
            if (next == belt::Press::None && belt::reached(s, t, now, p)) done = t.gen;
            link.publish(s, done, now);
            link.beat();
            ++now;
        }
    });
    belt::Target t;
    for (int i = 1; i <= 2000; ++i) {
        t.gen = static_cast<uint16_t>(i);
        t.running = (i % 7) != 0;
        t.speedTenths = 20 + (i % 4) * 5;
        link.setTarget(t);
        if (i % 50 == 0) std::this_thread::yield();
    }
    belt::Status st;
    for (int spin = 0; spin < 10000000; ++spin) {
        st = link.status(0);
        if (st.doneGen == t.gen) break;
        std::this_thread::yield();
    }
    stop.store(true);
    core1.join();
    CHECK_EQ(st.doneGen, t.gen);
    CHECK_EQ(st.running, t.running);
    CHECK_EQ(st.speedTenths, t.speedTenths);
    CHECK(link.heartbeat() > 0);
}

// ---------------------------------------------------------------------------
// The session state machine, against a simulated core 1
// ---------------------------------------------------------------------------

// Core 1 in miniature: one press per 400 ms (a burst plus the gap).
struct SimTreadmill {
    TreadmillLink link;
    belt::State st;
    uint16_t done = 0;
    uint32_t busyUntil = 0;
    int presses = 0;
    belt::Press lastPress = belt::Press::None;
    belt::Params p{1, 5, 5000};

    void step(uint32_t now) {
        if (static_cast<int32_t>(now - busyUntil) < 0) return;
        const belt::Target t = link.target();
        const belt::Press next = belt::next(st, t, now, p);
        if (next != belt::Press::None) {
            belt::apply(st, next, t, now, p);
            ++presses;
            lastPress = next;
            busyUntil = now + 400;
        } else {
            belt::apply(st, next, t, now, p);
            if (belt::reached(st, t, now, p)) done = t.gen;
        }
        link.publish(st, done, now);
    }
};

static const App::Timing TIMING{2000, 30000, 4000, true};

static plan::Params harnessParams() {
    plan::Params p = pyParams(false);  // the repeatable zig-zag,
    p.intervalS = 60;                  // one-minute segments
    p.defaultMin = 5;                  // in a five-minute session
    return p;
}

struct Harness {
    App app;
    SimTreadmill tm;
    uint32_t now = 0;

    explicit Harness(const App::Timing& timing = TIMING) : app(harnessParams(), timing, 1) { app.begin(0, false); }
    void pass(const Buttons& b = Buttons()) {
        tm.step(now);
        app.update(now, b, tm.link.status(now));
        tm.link.setTarget(app.target());
    }
    void run(uint32_t ms) {
        for (uint32_t end = now + ms; now < end; now += 10) pass();
    }
    // Run until `s` (or give up after `limitMs`).
    bool runUntil(App::State s, uint32_t limitMs = 60000) {
        for (uint32_t end = now + limitMs; now < end; now += 10) {
            pass();
            if (app.state() == s) return true;
        }
        return false;
    }
    void press(char which) {
        Buttons b;
        b.a = which == 'A', b.b = which == 'B', b.x = which == 'X', b.y = which == 'Y';
        pass(b);
        now += 10;
    }
    View view() const { return app.view(now, tm.link.status(now)); }
};

TEST(app_splash_then_planning) {
    Harness h;
    CHECK(h.app.state() == App::State::Splash);
    CHECK(h.view().screen == Screen::Splash);
    h.run(1990);
    CHECK(h.app.state() == App::State::Splash);
    h.run(20);
    CHECK(h.app.state() == App::State::Planning);
    CHECK_EQ(h.app.plan().n, 5);
    CHECK(!h.app.target().running);
}

TEST(app_whole_session) {
    Harness h;
    CHECK(h.runUntil(App::State::Planning));
    const plan::Plan planned = h.app.plan();

    // A: STARTING while the belt counts down and ramps; the clock waits
    h.press('A');
    CHECK(h.app.state() == App::State::Starting);
    CHECK(h.app.target().running);
    CHECK_EQ(h.app.target().speedTenths, planned.speed[0]);
    h.run(100);
    View v = h.view();
    CHECK(v.screen == Screen::Planning);
    CHECK(v.box == Box::Starting);
    CHECK_EQ(v.countdownS, 5);
    h.run(5000);
    v = h.view();
    CHECK_EQ(v.countdownS, 0);  // ramping now
    CHECK(v.speedTenths > 5);
    CHECK(h.runUntil(App::State::Running));
    CHECK_EQ(h.app.elapsedMs(), 0u);
    CHECK_EQ(h.tm.st.speedTenths, planned.speed[0]);
    const uint32_t startedAt = h.now;

    // into segment 1: new target and, as the speed changes, the triangle
    h.run(60010);
    CHECK_EQ(h.app.target().speedTenths, planned.speed[1]);
    v = h.view();
    CHECK(v.screen == Screen::SpeedChange);
    CHECK_EQ(v.up, planned.speed[1] > planned.speed[0]);
    CHECK_EQ(v.alertTenths, planned.speed[1]);
    h.run(4000);
    CHECK(h.view().screen == Screen::Session);
    CHECK_EQ(h.tm.st.speedTenths, planned.speed[1]);  // the ramp happened while the clock ran

    // A: pause stops the belt and the clock
    h.press('A');
    CHECK(h.app.state() == App::State::Paused);
    CHECK(!h.app.target().running);
    const uint32_t pausedAt = h.app.elapsedMs();
    h.run(30000);
    CHECK_EQ(h.app.elapsedMs(), pausedAt);
    CHECK(!h.tm.st.running);
    v = h.view();
    CHECK(v.box == Box::Paused);
    CHECK_EQ(v.speedTenths, planned.speed[1]);

    // A: resume restarts the belt at this segment's speed, then the clock
    h.press('A');
    CHECK(h.app.state() == App::State::Starting);
    CHECK(h.view().box == Box::Resuming);
    CHECK(h.view().screen == Screen::Session);
    CHECK_EQ(h.app.target().speedTenths, planned.speed[1]);
    CHECK(h.runUntil(App::State::Running));
    CHECK(h.app.elapsedMs() - pausedAt < 100);

    // to the end
    CHECK(h.runUntil(App::State::Completed, 400000));
    CHECK_EQ(h.app.elapsedMs(), 300000u);
    CHECK(!h.app.target().running);
    CHECK(h.now - startedAt > 300000u);  // the pause didn't count
    h.run(1000);
    CHECK(!h.tm.st.running);
    v = h.view();
    CHECK(v.screen == Screen::Completed);
    CHECK_EQ(v.leftS, 29u);
    CHECK_EQ(v.elapsedS, 300u);

    // a fresh plan after 30 s
    const uint32_t version = v.planVersion;
    CHECK(h.runUntil(App::State::Planning, 31000));
    CHECK(h.view().planVersion != version);
}

TEST(app_stop_during_start_keeps_the_plan) {
    Harness h;
    CHECK(h.runUntil(App::State::Planning));
    const uint32_t version = h.view().planVersion;
    h.press('A');
    h.run(1000);  // START sent, counting down
    CHECK(h.tm.st.running);
    h.press('X');
    CHECK(h.app.state() == App::State::Planning);
    CHECK(!h.app.target().running);
    CHECK_EQ(h.view().planVersion, version);
    h.run(1000);
    CHECK(!h.tm.st.running);
    CHECK(h.tm.lastPress == belt::Press::Stop);
}

TEST(app_stop_while_running_makes_a_new_plan) {
    Harness h;
    CHECK(h.runUntil(App::State::Planning));
    h.press('A');
    CHECK(h.runUntil(App::State::Running));
    const uint32_t version = h.view().planVersion;
    h.press('X');
    CHECK(h.app.state() == App::State::Planning);
    CHECK(!h.app.target().running);
    CHECK(h.view().planVersion != version);
}

TEST(app_planning_buttons) {
    Harness h;
    CHECK(h.runUntil(App::State::Planning));
    CHECK(h.view().editingTime);
    h.press('X');  // TIME 5 -> 10 min
    CHECK_EQ(h.app.session().durationMin, 10);
    CHECK_EQ(h.app.plan().n, 10);
    h.press('Y');
    h.press('Y');  // can't go under 5 min
    CHECK_EQ(h.app.session().durationMin, 5);
    h.press('B');  // now INTENSITY
    CHECK(!h.view().editingTime);
    const int before = h.view().boostTenths;
    h.press('X');
    CHECK_EQ(h.view().boostTenths, before + 10);  // one bar up 1.0 km/h
}

TEST(app_pause_without_stopping_the_belt) {
    Harness h(App::Timing{2000, 30000, 4000, false});
    CHECK(h.runUntil(App::State::Planning));
    h.press('A');
    CHECK(h.runUntil(App::State::Running));
    h.run(10000);
    h.press('A');
    CHECK(h.app.state() == App::State::Paused);
    CHECK(h.app.target().running);  // the belt keeps going
    const uint32_t pausedAt = h.app.elapsedMs();
    h.run(5000);
    h.press('A');
    CHECK(h.app.state() == App::State::Running);  // no restart needed
    h.run(1000);
    CHECK(h.app.elapsedMs() - pausedAt < 1100);
}

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------
TEST(button_one_press_through_bounce) {
    Button b(20, 0, UINT32_MAX);
    int presses = 0;
    // bouncy contact for 10 ms, then held 200 ms, bouncy release, quiet
    for (uint32_t t = 0; t < 400; ++t) {
        bool raw;
        if (t < 10) raw = (t % 3) == 0;
        else if (t < 210) raw = true;
        else if (t < 220) raw = (t % 2) == 0;
        else raw = false;
        if (b.update(raw, 1000 + t) == Button::Event::Pressed) ++presses;
    }
    CHECK_EQ(presses, 1);
    CHECK(!b.held());
}

// ---------------------------------------------------------------------------
// Canvas and screens
// ---------------------------------------------------------------------------
TEST(canvas_rects_text_and_triangles) {
    std::vector<uint16_t> buf(40 * 30);
    Canvas c(buf.data(), 40, 30);
    c.fill(0);
    c.fillRect(-5, -5, 10, 10, 1);  // clipped at the corner
    CHECK_EQ(c.get(0, 0), 1);
    CHECK_EQ(c.get(4, 4), 1);
    CHECK_EQ(c.get(5, 5), 0);
    c.fillRect(35, 25, 20, 20, 2);  // clipped at the far corner, no overrun
    CHECK_EQ(c.get(39, 29), 2);

    CHECK_EQ(Canvas::textWidth("ABC", 1), 17);
    CHECK_EQ(Canvas::textWidth("ABC", 3), 51);
    CHECK_EQ(Canvas::textWidth("", 2), 0);
    c.fill(0);
    c.text(20, 15, "I", 1, 3, Canvas::Align::Middle, Canvas::Align::Middle);
    CHECK_EQ(c.get(20, 15), 3);  // 'I' has a centre column; anchored on (20, 15)

    c.fill(0);
    c.fillTriangle(0, 29, 39, 29, 20, 0, 4);  // up
    CHECK_EQ(c.get(20, 1), 4);
    CHECK_EQ(c.get(1, 28), 4);
    CHECK_EQ(c.get(1, 2), 0);
    CHECK_EQ(c.get(38, 2), 0);
}

TEST(canvas_smooth_font) {
    const Font& f = fonts::POPPINS_BOLD_32;
    std::vector<uint16_t> buf(80 * 50);
    Canvas c(buf.data(), 80, 50);
    c.fill(0);
    c.blend(0, 0, rgb565(0xFFFFFF), 15);
    CHECK_EQ(c.get(0, 0), rgb565(0xFFFFFF));
    c.blend(1, 0, 0xFFFF, 0);
    CHECK_EQ(c.get(1, 0), 0);
    c.blend(2, 0, rgb565(0xFFFFFF), 8);  // about half way to white
    CHECK(((c.get(2, 0) >> 11) & 0x1F) >= 15 && ((c.get(2, 0) >> 11) & 0x1F) <= 17);

    // "I" is a solid bar: full coverage in its middle, nothing around it, and
    // the anti-aliasing leaves part-covered pixels somewhere along "O"
    CHECK_EQ(Canvas::textWidth(f, "II"), 2 * f.glyphs['I' - f.first].advance);
    c.fill(0);
    const int end = c.text(f, 10, 40, "I", rgb565(0xFFFFFF));
    CHECK_EQ(end, 10 + f.glyphs['I' - f.first].advance);
    const Glyph& gi = f.glyphs['I' - f.first];
    CHECK_EQ(c.get(10 + gi.xOff + gi.w / 2, 40 - f.capHeight / 2), rgb565(0xFFFFFF));
    CHECK_EQ(c.get(10 + gi.xOff + gi.w / 2, 40 - f.capHeight - 3), 0);  // above the capital
    CHECK_EQ(c.get(10 + gi.xOff + gi.w / 2, 42), 0);                    // below the baseline
    c.fill(0);
    c.text(f, 10, 40, "O", rgb565(0xFFFFFF));
    bool partial = false;
    for (int i = 0; i < 80 * 50; ++i) partial |= buf[i] != 0 && buf[i] != rgb565(0xFFFFFF);
    CHECK(partial);
}

TEST(speed_colour_matches_python) {
    // ui.py's _speed_color() for 1.8 .. 3.7 km/h; it uses floats, so allow
    // one step per channel either way
    const struct { int tenths; uint32_t rgb; } py[] = {
        {18, 0x00C83C}, {19, 0x00C83C}, {20, 0x00C83C}, {21, 0x20C933}, {22, 0x40CA2B}, {23, 0x5FCC24},
        {24, 0x7FCD1C}, {25, 0xA0CE14}, {26, 0xC0D00B}, {27, 0xE0D103}, {28, 0xEFC602}, {29, 0xEEB007},
        {30, 0xEC990D}, {31, 0xEB8212}, {32, 0xEA6B18}, {33, 0xE8551D}, {34, 0xE73E22}, {35, 0xE62828},
        {36, 0xE62828}, {37, 0xE62828},
    };
    for (const auto& e : py) {
        const uint32_t got = screens::speedColour(e.tenths);
        for (int shift : {16, 8, 0}) {
            const int a = (got >> shift) & 0xFF, b = (e.rgb >> shift) & 0xFF;
            CHECK(a - b <= 1 && b - a <= 1);
        }
    }
}

static const char* previewDir = nullptr;

// Binary PPM; test/ppm_to_png.py turns these into PNGs.
static void savePreview(const Canvas& c, const char* name) {
    if (!previewDir) return;
    const std::string path = std::string(previewDir) + "/" + name + ".ppm";
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", c.width(), c.height());
    for (int y = 0; y < c.height(); ++y)
        for (int x = 0; x < c.width(); ++x) {
            const uint16_t p = c.get(x, y);
            const uint8_t rgb[3] = {static_cast<uint8_t>((p >> 11) * 255 / 31),
                                    static_cast<uint8_t>(((p >> 5) & 0x3F) * 255 / 63),
                                    static_cast<uint8_t>((p & 0x1F) * 255 / 31)};
            std::fwrite(rgb, 1, 3, f);
        }
    std::fclose(f);
}

// True if the column x has any pixel of colour `c` between rows y0 and y1.
static bool columnHas(const Canvas& cv, int x, int y0, int y1, uint16_t c) {
    for (int y = y0; y < y1; ++y)
        if (cv.get(x, y) == c) return true;
    return false;
}

// Every screen at both pack sizes, from a session in progress.
TEST(screens_render_every_state) {
    struct Size { int w, h; const char* tag; } sizes[] = {{320, 240, "28"}, {240, 135, "114"}};
    plan::Plan p;
    const int16_t speeds[] = {20, 25, 30, 25, 30, 35, 30, 25, 20, 25, 30, 35};
    p.n = 12;
    for (int i = 0; i < p.n; ++i) p.speed[i] = speeds[i];

    for (const Size& sz : sizes) {
        std::vector<uint16_t> buf(sz.w * sz.h);
        Canvas c(buf.data(), sz.w, sz.h);
        const bool big = sz.h >= 160;
        const int chartBottom = sz.h - (big ? 16 : 12);
        char name[48];
        auto shot = [&](const View& v, const char* what) {
            screens::draw(c, v);
            std::snprintf(name, sizeof(name), "%s_%s", what, sz.tag);
            savePreview(c, name);
        };

        View v;
        v.screen = Screen::Splash;
        shot(v, "splash");
        // the name fits, one word per line, at the size the splash uses
        const Font& title = big ? fonts::POPPINS_BOLD_56 : fonts::POPPINS_BOLD_32;
        CHECK(Canvas::textWidth(title, "Treadmill") <= sz.w - 16);
        CHECK(Canvas::textWidth(title, "Buddy") <= sz.w - 16);
        CHECK(columnHas(c, sz.w / 2, 0, sz.h / 2, rgb565(0x00C8FF)));  // the name, in the accent colour

        v = View();
        v.screen = Screen::Planning;
        v.plan = &p;
        v.durationMin = 60;
        v.boostTenths = 20;
        v.distanceHkm = 275;
        shot(v, "planning");
        // the 3.5 km/h bar (index 5) stands in the top colour just above the baseline
        const int slot = (sz.w - 2 * (big ? 12 : 6)) / p.n;
        const int x5 = (big ? 12 : 6) + 5 * slot + slot / 2;
        CHECK_EQ(c.get(x5, chartBottom - 3), rgb565(screens::speedColour(35)));
        CHECK(Canvas::textWidth("2.75 km   X + / Y -   B swap   A start", 1) <= sz.w);

        v.box = Box::Starting;
        v.countdownS = 3;
        shot(v, "starting");

        v = View();
        v.screen = Screen::Session;
        v.plan = &p;
        v.elapsedS = 1530;
        v.remainingS = 2070;
        v.progress = 4250;
        v.speedTenths = 30;
        shot(v, "running");
        // the first bar is behind the tracker: greyed
        CHECK_EQ(c.get((big ? 12 : 6) + 2, chartBottom - 3), rgb565(0x555555));
        CHECK(!columnHas(c, x5, chartBottom - 10, chartBottom, rgb565(0x555555)));  // ahead: not

        v.paused = true;
        v.box = Box::Paused;
        v.speedTenths = 30;
        shot(v, "paused");
        CHECK_EQ(c.get(sz.w / 2 - (big ? 90 : 70), sz.h / 2), rgb565(0xFFAA00));

        v.paused = false;
        v.box = Box::Resuming;
        v.countdownS = 0;
        v.speedTenths = 14;
        shot(v, "resuming");

        v = View();
        v.screen = Screen::SpeedChange;
        v.up = true;
        v.alertTenths = 35;
        shot(v, "speed_up");
        CHECK_EQ(c.get(sz.w / 2, sz.h - 15), rgb565(screens::speedColour(35)));
        CHECK_EQ(c.get(sz.w / 2 - sz.h / 2 + 5, 15), 0);  // above the slope: black
        v.up = false;
        v.alertTenths = 20;
        shot(v, "speed_down");
        CHECK_EQ(c.get(sz.w / 2, 15), rgb565(screens::speedColour(20)));

        v = View();
        v.screen = Screen::Completed;
        v.plan = &p;
        v.elapsedS = 3600;
        v.distanceHkm = 275;
        v.leftS = 27;
        shot(v, "completed");
        CHECK(Canvas::textWidth("COMPLETE", big ? 5 : 2) <= sz.w);

        v = View();
        v.screen = Screen::RfTest;
        v.rfLast = belt::Press::Up;
        v.rfPresses = 3;
        shot(v, "rf_debug");
    }
}

int main() {
    previewDir = std::getenv("TREADMILL_PREVIEW_DIR");
    RUN(plan_boost_matches_python);
    RUN(plan_zigzag_matches_python);
    RUN(plan_planning_ops_match_python);
    RUN(plan_random_walk_rules);
    RUN(plan_distance);
    RUN(belt_start_counts_down_then_ramps);
    RUN(belt_stop_interrupts_the_countdown);
    RUN(belt_part_presses_round_like_python);
    RUN(link_round_trip);
    RUN(link_raw_presses);
    RUN(link_two_threads_latest_target_wins);
    RUN(app_splash_then_planning);
    RUN(app_whole_session);
    RUN(app_stop_during_start_keeps_the_plan);
    RUN(app_stop_while_running_makes_a_new_plan);
    RUN(app_planning_buttons);
    RUN(app_pause_without_stopping_the_belt);
    RUN(button_one_press_through_bounce);
    RUN(canvas_rects_text_and_triangles);
    RUN(canvas_smooth_font);
    RUN(speed_colour_matches_python);
    RUN(screens_render_every_state);
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
