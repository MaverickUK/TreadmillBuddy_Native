#include "screens.h"

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "fonts/poppins_bold_32.h"
#include "fonts/poppins_bold_56.h"
#include "fonts/poppins_semibold_14.h"
#include "fonts/poppins_semibold_20.h"
#include "version.h"

namespace screens {

namespace {

using Align = Canvas::Align;

constexpr uint16_t BLACK = rgb565(0x000000);
constexpr uint16_t WHITE = rgb565(0xFFFFFF);
constexpr uint16_t GREY = rgb565(0x787878);
constexpr uint16_t GREEN = rgb565(0x00D25A);
constexpr uint16_t AMBER = rgb565(0xFFAA00);
constexpr uint16_t ACCENT = rgb565(0x00C8FF);
constexpr uint16_t RED = rgb565(0xFF3030);
constexpr uint16_t PAST_GREY = rgb565(0x555555);

// Positions and text scales for the panel size (ui.py's _layout()).
struct Layout {
    int W, H;
    bool big;
    int chartLeft, chartTop, chartBottom;
    int sTitle, sMode, sSpeed, sKmh, sTval, sDone, sStats, sBox;

    Layout(int w, int h) : W(w), H(h), big(h >= 160) {
        chartLeft = big ? 12 : 6;
        chartTop = big ? 90 : 48;
        chartBottom = H - (big ? 16 : 12);
        sTitle = big ? 3 : 2;
        sMode = big ? 2 : 1;
        sSpeed = big ? 6 : 3;
        sKmh = big ? 2 : 1;
        sTval = big ? 2 : 1;
        sDone = big ? 5 : 2;
        sStats = big ? 2 : 1;
        sBox = big ? 4 : 2;
    }
    int chartWidth() const { return W - 2 * chartLeft; }
};

void fmtTenths(char* out, size_t n, int tenths) {
    const int mag = tenths < 0 ? -tenths : tenths;
    snprintf(out, n, "%s%d.%d", tenths < 0 ? "-" : "", mag / 10, mag % 10);
}

void fmtTime(char* out, size_t n, uint32_t seconds) {
    snprintf(out, n, "%02lu:%02lu", static_cast<unsigned long>(seconds / 60), static_cast<unsigned long>(seconds % 60));
}

void fmtKm(char* out, size_t n, uint32_t hundredths) {
    snprintf(out, n, "%lu.%02lu", static_cast<unsigned long>(hundredths / 100),
             static_cast<unsigned long>(hundredths % 100));
}

// a + (b - a) * num / den, rounded down like Python's int() on the positive result
int lerp(int a, int b, int num, int den) { return (a * den + (b - a) * num) / den; }

struct Bar {
    int x, y, w, h;
};

// Bars butt up against each other: each runs from its slot's left edge to
// the next one's, so rounding never leaves a gap.
Bar barFor(const Layout& L, const plan::Plan& p, int i) {
    const int lo = cfg::MIN_SPEED_TENTHS, span = cfg::MAX_SPEED_TENTHS - lo;
    const int usable = L.chartBottom - L.chartTop;
    const int minBar = L.big ? 14 : 10;
    const int area = L.chartWidth();
    int frac = p.speed[i] - lo;  // of span
    if (frac < 0) frac = 0;
    if (frac > span) frac = span;
    Bar b;
    b.h = minBar + (span > 0 ? frac * (usable - minBar) / span : 0);
    b.x = L.chartLeft + i * area / p.n;
    const int next = L.chartLeft + (i + 1) * area / p.n;
    b.w = next - b.x > 1 ? next - b.x : 1;
    b.y = L.chartBottom - b.h;
    return b;
}

// The bar chart with a speed over each bar, and the baseline.
void drawChart(Canvas& c, const Layout& L, const plan::Plan& p) {
    char buf[8];
    for (int i = 0; i < p.n; ++i) {
        const Bar b = barFor(L, p, i);
        c.fillRect(b.x, b.y, b.w, b.h, rgb565(speedColour(p.speed[i])));
        // "0.0" needs 3 glyphs: drop the labels rather than let them overlap
        // once the bars get narrow (long sessions)
        if (b.w >= 3 * Canvas::CHAR_W) {
            fmtTenths(buf, sizeof(buf), p.speed[i]);
            c.text(b.x + b.w / 2, b.y - 2, buf, 1, GREY, Align::Middle, Align::End);
        }
    }
    c.fillRect(L.chartLeft, L.chartBottom, L.chartWidth(), 2, GREY);
}

// Grey over the part of each bar already walked, and the progress line.
void drawProgress(Canvas& c, const Layout& L, const plan::Plan& p, uint16_t progress) {
    const int px = L.chartLeft + static_cast<int>(static_cast<int32_t>(progress) * L.chartWidth() / View::PROGRESS_FULL);
    for (int i = 0; i < p.n; ++i) {
        const Bar b = barFor(L, p, i);
        if (px >= b.x + b.w) c.fillRect(b.x, b.y, b.w, b.h, PAST_GREY);  // passed
        else if (px > b.x) c.fillRect(b.x, b.y, px - b.x, b.h, PAST_GREY);  // under the tracker
    }
    c.fillRect(px, L.chartTop - 4, 2, (L.chartBottom - L.chartTop) + 4, ACCENT);
}

// A message box in the middle of the screen, with a black border; `sub`
// (optional) is a second line inside it, under the main text.
void drawBox(Canvas& c, const Layout& L, int bw, int bh, uint16_t colour, const char* text, const char* sub) {
    const int x = (L.W - bw) / 2, y = (L.H - bh) / 2;
    c.fillRect(x - 3, y - 3, bw + 6, bh + 6, BLACK);
    c.fillRect(x, y, bw, bh, colour);
    if (!sub) {
        c.text(L.W / 2, L.H / 2, text, L.sBox, BLACK, Align::Middle, Align::Middle);
        return;
    }
    const int line = L.big ? 26 : 14;  // the sub line's share of the box
    c.text(L.W / 2, y + (bh - line) / 2, text, L.sBox, BLACK, Align::Middle, Align::Middle);
    c.text(L.W / 2, y + bh - line / 2 - 1, sub, L.big ? 2 : 1, BLACK, Align::Middle, Align::Middle);
}

void drawSplash(Canvas& c, const Layout& L, const View& v) {
    // The name goes one line per word in Poppins Bold, smooth-edged; a word
    // too wide for the panel drops to the smaller size.
    const char* words[4];
    char name[32];
    strncpy(name, cfg::APP_NAME, sizeof(name) - 1);
    name[sizeof(name) - 1] = 0;
    int count = 0;
    const Font* font = L.big ? &fonts::POPPINS_BOLD_56 : &fonts::POPPINS_BOLD_32;
    for (char* w = strtok(name, " "); w && count < 4; w = strtok(nullptr, " ")) {
        words[count++] = w;
        if (Canvas::textWidth(*font, w) > L.W - 16) font = &fonts::POPPINS_BOLD_32;
    }

    // Poppins' own line height is loose; set the lines a little apart,
    // and centre the block of capitals on centreY.
    const int lineH = font->capHeight * 135 / 100;
    const int centreY = L.H * 38 / 100;
    const int firstBaseline = centreY - (font->capHeight + (count - 1) * lineH) / 2 + font->capHeight;
    for (int i = 0; i < count; ++i)
        c.text(*font, L.W / 2, firstBaseline + i * lineH, words[i], ACCENT, Align::Middle, Align::End);

    char buf[48];
    const int authY = L.big ? 175 : 96;
    snprintf(buf, sizeof(buf), "by %s", cfg::APP_AUTHOR);
    c.text(L.big ? fonts::POPPINS_SEMIBOLD_20 : fonts::POPPINS_SEMIBOLD_14, L.W / 2, authY, buf, GREY,
           Align::Middle, Align::Middle);
    c.text(L.W / 2, authY + (L.big ? 20 : 14), "v" FIRMWARE_VERSION, 1, GREY, Align::Middle, Align::Middle);
    if (v.recovered)
        c.text(L.W / 2, L.H - 8, "recovered from a fault: belt stopped", 1, RED, Align::Middle, Align::Middle);
}

void drawPlanning(Canvas& c, const Layout& L, const View& v) {
    drawChart(c, L, *v.plan);

    const int titleY = L.big ? 20 : 10, modeY = L.big ? 52 : 27, hintY = L.big ? 78 : 39;
    char buf[48], num[12];
    c.text(L.W / 2, titleY, "SESSION PLAN", L.sTitle, WHITE, Align::Middle, Align::Middle);

    snprintf(buf, sizeof(buf), "TIME %dm", v.durationMin);
    c.text(L.chartLeft, modeY, buf, L.sMode, v.editingTime ? ACCENT : GREY, Align::Start, Align::Middle);

    // the boost reads as a running total of speed added across the session:
    // every X press lifts one bar by 1.0 km/h, every Y drops one
    fmtTenths(num, sizeof(num), v.boostTenths < 0 ? -v.boostTenths : v.boostTenths);
    snprintf(buf, sizeof(buf), "SPEED %s%s", v.boostTenths > 0 ? "+" : (v.boostTenths < 0 ? "-" : ""), num);
    c.text(L.W - L.chartLeft, modeY, buf, L.sMode, v.editingTime ? GREY : ACCENT, Align::End, Align::Middle);

    fmtKm(num, sizeof(num), v.distanceHkm);
    snprintf(buf, sizeof(buf), "%s km   X + / Y -   B swap   A start", num);
    c.text(L.W / 2, hintY, buf, 1, GREY, Align::Middle, Align::Middle);
}

void drawSession(Canvas& c, const Layout& L, const View& v) {
    drawChart(c, L, *v.plan);
    drawProgress(c, L, *v.plan, v.progress);

    // big current speed top-left; elapsed and remaining side by side to its right
    char buf[16];
    int elX, rmX, titleY, valY;
    fmtTenths(buf, sizeof(buf), v.speedTenths);
    if (L.big) {
        c.text(12, 8, buf, L.sSpeed, v.paused ? AMBER : GREEN);
        c.text(150, 78, "km/h", L.sKmh, GREY, Align::Start, Align::End);
        elX = 140, rmX = 230, titleY = 10, valY = 26;
    } else {
        c.text(6, 4, buf, L.sSpeed, v.paused ? AMBER : GREEN);
        c.text(64, 28, "km/h", L.sKmh, GREY, Align::Start, Align::End);
        elX = 90, rmX = 160, titleY = 4, valY = 14;
    }
    c.text(elX, titleY, "ELAPSED", 1, GREY);
    fmtTime(buf, sizeof(buf), v.elapsedS);
    c.text(elX, valY, buf, L.sTval, WHITE);
    c.text(rmX, titleY, "REMAINING", 1, GREY);
    fmtTime(buf, sizeof(buf), v.remainingS);
    c.text(rmX, valY, buf, L.sTval, WHITE);
}

// STARTING / RESUMING: the treadmill's countdown, then the ramp up to speed.
void drawStartBox(Canvas& c, const Layout& L, const View& v) {
    char sub[24], num[8];
    if (v.countdownS) {
        snprintf(sub, sizeof(sub), "belt in %us", v.countdownS);
    } else {
        fmtTenths(num, sizeof(num), v.speedTenths);
        snprintf(sub, sizeof(sub), "ramping %s km/h", num);
    }
    const int bw = L.big ? 240 : 190, bh = L.big ? 92 : 54;
    drawBox(c, L, bw, bh, GREEN, v.box == Box::Resuming ? "RESUMING" : "STARTING", sub);
}

void drawSpeedChange(Canvas& c, const Layout& L, const View& v) {
    const int margin = 10;
    const int th = L.H - 2 * margin;
    int tw = L.H * 105 / 100;
    if (tw > L.W - 2 * margin) tw = L.W - 2 * margin;
    const int x = (L.W - tw) / 2, y = margin;
    const uint16_t colour = rgb565(speedColour(v.alertTenths));
    if (v.up) c.fillTriangle(x, y + th, x + tw, y + th, x + tw / 2, y, colour);
    else c.fillTriangle(x, y, x + tw, y, x + tw / 2, y + th, colour);
}

void drawCompleted(Canvas& c, const Layout& L, const View& v) {
    const int ys[4] = {L.big ? 55 : 28, L.big ? 105 : 52, L.big ? 155 : 82, L.big ? 195 : 104};
    c.text(L.W / 2, ys[0], "SESSION", L.sDone, GREEN, Align::Middle, Align::Middle);
    c.text(L.W / 2, ys[1], "COMPLETE", L.sDone, GREEN, Align::Middle, Align::Middle);
    char t[16], km[16], buf[48];
    fmtTime(t, sizeof(t), v.elapsedS);
    fmtKm(km, sizeof(km), v.distanceHkm);
    snprintf(buf, sizeof(buf), "%s  %s km", t, km);
    c.text(L.W / 2, ys[2], buf, L.sStats, WHITE, Align::Middle, Align::Middle);
    snprintf(buf, sizeof(buf), "new plan in %lus", static_cast<unsigned long>(v.leftS));
    c.text(L.W / 2, ys[3], buf, L.sStats, GREY, Align::Middle, Align::Middle);
}

// The RF test mode (debug_rf.py): A/B/X/Y send START/STOP/UP/DOWN.
void drawRfTest(Canvas& c, const Layout& L, const View& v) {
    c.text(L.W / 2, 6, "RF DEBUG", L.sTitle, ACCENT, Align::Middle, Align::Start);
    const char* name = "Ready";
    uint16_t colour = WHITE;
    switch (v.rfLast) {
        case belt::Press::Start: name = "START"; colour = GREEN; break;
        case belt::Press::Stop: name = "STOP"; colour = RED; break;
        case belt::Press::Up: name = "UP"; colour = ACCENT; break;
        case belt::Press::Down: name = "DOWN"; colour = AMBER; break;
        case belt::Press::None: break;
    }
    c.text(L.W / 2, L.H / 2, name, L.sSpeed, colour, Align::Middle, Align::Middle);
    char buf[32];
    if (v.rfSending) snprintf(buf, sizeof(buf), "sending...");
    else if (v.rfPresses) snprintf(buf, sizeof(buf), "presses: %lu", static_cast<unsigned long>(v.rfPresses));
    else snprintf(buf, sizeof(buf), "A=Start B=Stop X=Up Y=Down");
    c.text(L.W / 2, L.H - 6, buf, 1, GREY, Align::Middle, Align::End);
}

} // namespace

uint32_t speedColour(int tenths) {
    // Routing through yellow keeps the middle speeds vivid and distinct
    // instead of the muddy brown a straight green to red blend would give.
    // t = (tenths - lo) / span, doubled so each half of the blend runs 0..span
    const int lo = cfg::MIN_SPEED_TENTHS, span = cfg::MAX_SPEED_TENTHS - lo;
    if (span <= 0) return 0x00C83C;
    int t2 = 2 * (tenths - lo);
    if (t2 < 0) t2 = 0;
    if (t2 > 2 * span) t2 = 2 * span;
    int r, g, b;
    if (t2 < span) {  // green -> yellow
        r = lerp(0, 240, t2, span), g = lerp(200, 210, t2, span), b = lerp(60, 0, t2, span);
    } else {  // yellow -> red
        r = lerp(240, 230, t2 - span, span), g = lerp(210, 40, t2 - span, span), b = lerp(0, 40, t2 - span, span);
    }
    return static_cast<uint32_t>(r) << 16 | static_cast<uint32_t>(g) << 8 | static_cast<uint32_t>(b);
}

void draw(Canvas& c, const View& v) {
    const Layout L(c.width(), c.height());
    c.fill(BLACK);
    switch (v.screen) {
        case Screen::Splash: drawSplash(c, L, v); break;
        case Screen::Planning: drawPlanning(c, L, v); break;
        case Screen::Session: drawSession(c, L, v); break;
        case Screen::SpeedChange: drawSpeedChange(c, L, v); break;
        case Screen::Completed: drawCompleted(c, L, v); break;
        case Screen::RfTest: drawRfTest(c, L, v); break;
    }
    switch (v.box) {
        case Box::None: break;
        case Box::Paused: drawBox(c, L, L.big ? 200 : 150, L.big ? 66 : 40, AMBER, "PAUSED", nullptr); break;
        case Box::Starting:
        case Box::Resuming: drawStartBox(c, L, v); break;
    }
}

} // namespace screens
