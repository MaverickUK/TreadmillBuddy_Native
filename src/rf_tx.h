// OOK transmitter for the STX882: replays captured mark/space timings on its
// DATA pin. Core 1 only.
//
// A PIO state machine does the timing, at 1 µs per tick: each FIFO word is
// one mark or space length, and the PIO holds the pin high or low for
// exactly that long, whatever the CPUs are doing. (The CircuitPython version
// needed pulseio for the same reason: a software loop's jitter makes the
// treadmill's decoder miss frames.) Between bursts the pin idles low, so the
// transmitter is silent.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace rf {

// Claim a PIO state machine and drive `pin` low.
bool begin(uint8_t pin);

// Send `lead` once, then `code` `repeats` times back-to-back, as one
// continuous burst. Blocks until the last space is over; calls `tick` as it
// goes (the core's heartbeat).
void send(const uint16_t* lead, size_t leadLen, const uint16_t* code, size_t codeLen, int repeats, void (*tick)());

} // namespace rf
