// Treadmill Buddy firmware.
//
// Core 1: the treadmill controller only (treadmill.*): RF presses towards
//         the belt target, through a PIO state machine.
// Core 0: everything else: buttons, the session state machine (app.*),
//         drawing (screens.*) and the LCD (st7789.*, by DMA), the serial
//         console and the watchdog. It never blocks on the treadmill.
//
// Hold B while powering up for the RF test mode: A/B/X/Y send
// START/STOP/UP/DOWN (the CircuitPython debug_rf.py).
//
// Serial commands (115200, USB): h help, s status, 1 START, 0 STOP,
// + SPEED_UP, - SPEED_DOWN, r reboot, b reboot to USB bootloader.
#include <Arduino.h>

#include "app.h"
#include "button.h"
#include "canvas.h"
#include "config.h"
#include "screens.h"
#include "st7789.h"
#include "status_led.h"
#include "treadmill.h"
#include "version.h"

#ifndef WAIT_FOR_SERIAL_MS
#define WAIT_FOR_SERIAL_MS 0
#endif

// ---------------------------------------------------------------------------
// Core 1
// ---------------------------------------------------------------------------
void setup1() { treadmill::core1Setup(); }
void loop1() { treadmill::core1Loop(); }

// ---------------------------------------------------------------------------
// Core 0
// ---------------------------------------------------------------------------
namespace {

using treadmill::link;

uint16_t framebuffer[cfg::DISPLAY_WIDTH * cfg::DISPLAY_HEIGHT];  // 150 KB on the 2.8" pack
Canvas canvas(framebuffer, cfg::DISPLAY_WIDTH, cfg::DISPLAY_HEIGHT);

const uint8_t BUTTON_PINS[4] = {cfg::PIN_BUTTON_A, cfg::PIN_BUTTON_B, cfg::PIN_BUTTON_X, cfg::PIN_BUTTON_Y};
// Only Pressed is used: no long presses, so those limits are out of reach.
Button buttons[4] = {
    {cfg::DEBOUNCE_MS, 0, UINT32_MAX}, {cfg::DEBOUNCE_MS, 0, UINT32_MAX},
    {cfg::DEBOUNCE_MS, 0, UINT32_MAX}, {cfg::DEBOUNCE_MS, 0, UINT32_MAX},
};

App* app = nullptr;  // built in setup(), once there's a random seed
bool rfTestMode = false;
belt::Press rfLast = belt::Press::None;
uint32_t rfPresses = 0;

View shownView;
bool drawnOnce = false;
uint32_t framesDrawn = 0;
uint32_t lastRenderUs = 0;

uint32_t lastHeartbeat = 0;
uint32_t lastHeartbeatChangeMs = 0;
bool core1Stalled = false;

// ---- helpers ---------------------------------------------------------------

// The watchdog is fed only while core 1's heartbeat advances: if either core
// hangs, the board resets (and the next boot sends STOP).
void feedWatchdog() {
    if (!core1Stalled) rp2040.wdt_reset();
}

void checkCore1() {
    const uint32_t hb = link.heartbeat();
    const uint32_t now = millis();
    if (hb != lastHeartbeat) {
        lastHeartbeat = hb;
        lastHeartbeatChangeMs = now;
    } else if (!core1Stalled && now - lastHeartbeatChangeMs > cfg::CORE1_STALL_MS) {
        core1Stalled = true;  // stop feeding: the watchdog resets us
        Serial.println("[fatal] treadmill core stalled, letting the watchdog reset");
    }
}

Buttons readButtons(uint32_t now) {
    bool hit[4];
    for (int i = 0; i < 4; ++i)
        hit[i] = buttons[i].update(digitalRead(BUTTON_PINS[i]) == LOW, now) == Button::Event::Pressed;
    Buttons b;
    b.a = hit[0], b.b = hit[1], b.x = hit[2], b.y = hit[3];
    return b;
}

const char* pressName(belt::Press p) {
    switch (p) {
        case belt::Press::Start: return "START";
        case belt::Press::Stop: return "STOP";
        case belt::Press::Up: return "SPEED_UP";
        case belt::Press::Down: return "SPEED_DOWN";
        case belt::Press::None: break;
    }
    return "-";
}

void sendRaw(belt::Press p) {
    link.requestRaw(p);
    Serial.printf("[rf] %s requested (raw: the tracked belt speed doesn't change)\n", pressName(p));
}

uint32_t ledColour(App::State s) {
    switch (s) {
        case App::State::Splash: return 0xFFFFFF;
        case App::State::Planning: return 0x005AC8;
        case App::State::Starting:
        case App::State::Running: return 0x00D25A;
        case App::State::Paused: return 0xFF9600;
        case App::State::Completed: return 0x8C00DC;
    }
    return 0;
}

void printStatus() {
    const belt::Status s = link.status(millis());
    if (rfTestMode) {
        Serial.printf("[status] t=%lus mode=RF_TEST presses=%lu\n", static_cast<unsigned long>(millis() / 1000),
                      static_cast<unsigned long>(link.pressesSent()));
        return;
    }
    const belt::Target& t = app->target();
    Serial.printf("[status] t=%lus state=%s elapsed=%lus/%lus plan=%d segments | target: %s %d.%d km/h gen=%u | "
                  "belt: %s %d.%d km/h countdown=%lums done_gen=%u presses=%lu | frame=%luus render=%luus frames=%lu heap=%d\n",
                  static_cast<unsigned long>(millis() / 1000), App::stateName(app->state()),
                  static_cast<unsigned long>(app->elapsedMs() / 1000),
                  static_cast<unsigned long>(app->session().durationMin * 60u), app->plan().n,
                  t.running ? "run" : "stop", t.speedTenths / 10, t.speedTenths % 10, t.gen,
                  s.running ? "run" : "stop", s.speedTenths / 10, s.speedTenths % 10,
                  static_cast<unsigned long>(s.countdownLeftMs), s.doneGen,
                  static_cast<unsigned long>(link.pressesSent()), static_cast<unsigned long>(lcd::lastFrameUs()),
                  static_cast<unsigned long>(lastRenderUs), static_cast<unsigned long>(framesDrawn),
                  rp2040.getFreeHeap());
}

void printHelp() {
    Serial.println("h help, s status, 1 START, 0 STOP, + SPEED_UP, - SPEED_DOWN (raw presses), "
                   "r reboot, b USB bootloader");
}

void handleSerial() {
    while (Serial.available()) {
        switch (Serial.read()) {
            case 'h': printHelp(); break;
            case 's': printStatus(); break;
            case '1': sendRaw(belt::Press::Start); break;
            case '0': sendRaw(belt::Press::Stop); break;
            case '+': sendRaw(belt::Press::Up); break;
            case '-': sendRaw(belt::Press::Down); break;
            case 'r': rp2040.reboot(); break;
            case 'b': rp2040.rebootToBootloader(); break;
            default: break;
        }
    }
}

// The RF test mode: each button sends one raw press.
void updateRfTest(const Buttons& b) {
    belt::Press p = belt::Press::None;
    if (b.a) p = belt::Press::Start;
    else if (b.b) p = belt::Press::Stop;
    else if (b.x) p = belt::Press::Up;
    else if (b.y) p = belt::Press::Down;
    if (p == belt::Press::None) return;
    ++rfPresses;
    rfLast = p;
    link.requestRaw(p);
    Serial.printf("[%lu] %s pressed -> transmitting\n", static_cast<unsigned long>(rfPresses), pressName(p));
}

// Redraw when what's shown would change, and only once the last frame has
// gone out (its DMA reads the framebuffer we'd be drawing into).
void render(const View& v) {
    if (drawnOnce && v == shownView) return;
    if (lcd::busy()) return;  // try again next pass
    const uint32_t start = micros();
    screens::draw(canvas, v);
    lastRenderUs = micros() - start;
    lcd::pushFrame(framebuffer);
    shownView = v;
    drawnOnce = true;
    ++framesDrawn;
}

} // namespace

void setup() {
    lcd::begin(framebuffer);  // first: holds the backlight off until a black frame is up
    Serial.begin(115200);
#if WAIT_FOR_SERIAL_MS > 0
    while (!Serial && millis() < WAIT_FOR_SERIAL_MS) delay(10);
#endif
    for (uint8_t pin : BUTTON_PINS) pinMode(pin, INPUT_PULLUP);
    status_led::begin();

    const bool watchdogReset = rp2040.getResetReason() == RP2040::WDT_RESET;
    Serial.printf("\n=== Treadmill Buddy v%s (%dx%d) ===\n", FIRMWARE_VERSION, cfg::DISPLAY_WIDTH, cfg::DISPLAY_HEIGHT);
    if (watchdogReset) {
        // The belt may still be running from before the reset: stop it.
        Serial.println("[boot] watchdog reset: sending STOP");
        link.requestRaw(belt::Press::Stop);
    }

    delay(5);  // let the pull-ups settle before reading B
    rfTestMode = digitalRead(cfg::PIN_BUTTON_B) == LOW;
    if (rfTestMode) {
        Serial.println("[boot] B held: RF test mode (A START, B STOP, X UP, Y DOWN)");
        // the press that chose this mode isn't a test press
        const uint32_t now = millis();
        for (int i = 0; i < 4; ++i) buttons[i].update(digitalRead(BUTTON_PINS[i]) == LOW, now);
        buttons[1].update(true, now + cfg::DEBOUNCE_MS);
    }

    static App theApp(plan::DEFAULT_PARAMS, App::DEFAULT_TIMING, rp2040.hwrand32());
    app = &theApp;
    app->begin(millis(), watchdogReset);
    printHelp();

    rp2040.wdt_begin(cfg::WATCHDOG_MS);
}

void loop() {
    feedWatchdog();
    checkCore1();
    handleSerial();
    lcd::busy();  // ends (and times) a frame as soon as its DMA is done

    const uint32_t now = millis();
    const Buttons b = readButtons(now);
    const belt::Status belt = link.status(now);

    View v;
    if (rfTestMode) {
        updateRfTest(b);
        v.screen = Screen::RfTest;
        v.rfLast = rfLast;
        v.rfPresses = rfPresses;
        v.rfSending = link.rawPending();
    } else {
        const App::State before = app->state();
        app->update(now, b, belt);
        link.setTarget(app->target());
        if (app->state() != before) Serial.printf("[state] %s -> %s\n", App::stateName(before), App::stateName(app->state()));
        status_led::set(ledColour(app->state()));
        v = app->view(now, belt);
    }
    render(v);
}
