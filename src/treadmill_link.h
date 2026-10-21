// Everything the two cores share: the belt target (core 0 writes), the
// belt status and a heartbeat (core 1 writes), and one-off raw presses for
// the RF test mode and the serial console. Pure C++ (std::atomic only),
// host-tested with two threads.
//
// Each atomic has exactly one writer, and only plain load/store are used:
// the RP2040's Cortex-M0+ has no atomic read-modify-write instructions, so
// fetch_add and friends would need a lock. A struct that has to change as a
// whole (target, status) is packed into one 32-bit word so the other core
// never sees half an update.
#pragma once

#include <atomic>
#include <stdint.h>

#include "belt.h"

class TreadmillLink {
public:
    // ---- core 0 ----
    void setTarget(const belt::Target& t) { target_.store(packTarget(t)); }
    belt::Status status(uint32_t nowMs) const {
        belt::Status s;
        const uint32_t w = status_.load();
        s.doneGen = static_cast<uint16_t>(w >> 16);
        s.running = w & 0x8000u;
        s.speedTenths = static_cast<int>(w & 0x3FFFu);
        if (w & 0x4000u) {
            const int32_t left = static_cast<int32_t>(countdownEndMs_.load() - nowMs);
            s.countdownLeftMs = left > 0 ? static_cast<uint32_t>(left) : 0;
        }
        return s;
    }
    // Ask core 1 to send one raw press (no change to the tracked belt state).
    // A newer request replaces one not yet sent.
    void requestRaw(belt::Press p) {
        rawSeq_ = static_cast<uint16_t>(rawSeq_ + 1);
        rawRequest_.store(static_cast<uint32_t>(rawSeq_) << 8 | static_cast<uint8_t>(p));
    }
    bool rawPending() const { return (rawRequest_.load() >> 8) != rawDone_.load(); }
    uint32_t heartbeat() const { return heartbeat_.load(); }
    uint32_t pressesSent() const { return pressesSent_.load(); }

    // ---- core 1 ----
    belt::Target target() const { return unpackTarget(target_.load()); }
    void publish(const belt::State& s, uint16_t doneGen, uint32_t nowMs) {
        const bool counting = s.running && s.counting && static_cast<int32_t>(nowMs - s.countdownEndMs) < 0;
        countdownEndMs_.store(s.countdownEndMs);  // before the status word that says to read it
        status_.store(static_cast<uint32_t>(doneGen) << 16 | (s.running ? 0x8000u : 0) |
                      (counting ? 0x4000u : 0) | (static_cast<uint32_t>(s.speedTenths) & 0x3FFFu));
    }
    // A raw press to send, or None. Call rawSent() once it's out.
    belt::Press takeRaw() {
        const uint32_t r = rawRequest_.load();
        if ((r >> 8) == rawDone_.load()) return belt::Press::None;
        rawTaking_ = r >> 8;
        return static_cast<belt::Press>(r & 0xFF);
    }
    void rawSent() { rawDone_.store(rawTaking_); }
    void beat() { heartbeat_.store(heartbeat_.load() + 1); }
    void countPress() { pressesSent_.store(pressesSent_.load() + 1); }

private:
    static uint32_t packTarget(const belt::Target& t) {
        return static_cast<uint32_t>(t.gen) << 16 | (t.running ? 0x8000u : 0) |
               (static_cast<uint32_t>(t.speedTenths) & 0x7FFFu);
    }
    static belt::Target unpackTarget(uint32_t w) {
        belt::Target t;
        t.gen = static_cast<uint16_t>(w >> 16);
        t.running = w & 0x8000u;
        t.speedTenths = static_cast<int>(w & 0x7FFFu);
        return t;
    }

    std::atomic<uint32_t> target_{0};          // core 0 writes
    std::atomic<uint32_t> rawRequest_{0};      // core 0 writes: seq << 8 | press
    uint16_t rawSeq_ = 0;                      // core 0 only
    std::atomic<uint32_t> status_{0};          // core 1 writes
    std::atomic<uint32_t> countdownEndMs_{0};  // core 1 writes
    std::atomic<uint32_t> rawDone_{0};         // core 1 writes: last raw seq sent
    uint32_t rawTaking_ = 0;                   // core 1 only
    std::atomic<uint32_t> heartbeat_{0};       // core 1 writes
    std::atomic<uint32_t> pressesSent_{0};     // core 1 writes
};
