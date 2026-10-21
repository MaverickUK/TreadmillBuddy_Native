// Captured OOK RF codes for the treadmill's own remote (a JTX MoveLight).
//
// Each code is a mark/space timing capture in microseconds, alternating
// HIGH, LOW, HIGH, LOW... starting with a mark, recorded from the remote.
// Replaying one on the STX882's DATA pin re-creates the same OOK envelope,
// so the treadmill can't tell the difference. Re-capture and replace these
// for a different treadmill (the CircuitPython rf_codes.py has the same data).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace rf_codes {

// Sent once per burst so the receiver locks on to the first copy.
constexpr uint16_t LEAD_SYNC[] = {306, 9377};

constexpr uint16_t SPEED_UP[] = {
    356, 854, 352, 853, 353, 852, 352, 853, 352, 852, 352, 853, 352,
    853, 353, 856, 903, 350, 308, 850, 902, 351, 856, 349, 857, 350,
    855, 350, 856, 350, 855, 353, 858, 348, 309, 850, 903, 349, 309,
    849, 352, 854, 902, 350, 308, 850, 902, 347, 307, 9373,
};

constexpr uint16_t SPEED_DOWN[] = {
    356, 854, 352, 852, 352, 851, 352, 851, 352, 851, 352, 851, 353,
    851, 352, 856, 901, 351, 307, 849, 899, 352, 855, 349, 854, 350,
    854, 350, 854, 350, 853, 352, 856, 349, 307, 850, 900, 351, 308,
    848, 352, 851, 352, 853, 901, 350, 854, 347, 307, 9367,
};

constexpr uint16_t START[] = {
    355, 854, 352, 852, 352, 851, 353, 851, 352, 852, 352, 851, 353, 851,
    352, 856, 901, 351, 308, 849, 902, 350, 856, 350, 855, 350, 854, 350,
    854, 351, 855, 352, 856, 350, 307, 850, 902, 349, 308, 849, 352, 852,
    352, 852, 352, 853, 902, 347, 307, 9366,
};

constexpr uint16_t STOP[] = {
    352, 857, 349, 855, 349, 854, 350, 854, 349, 855, 349, 855, 349, 855,
    349, 859, 902, 348, 307, 852, 902, 348, 856, 350, 855, 350, 856, 350,
    856, 349, 858, 350, 857, 348, 308, 852, 899, 350, 307, 853, 349, 854,
    349, 855, 901, 349, 307, 848, 349, 9372,
};

// Every code must end on a space (an even count), so back-to-back copies
// keep marks and spaces in step.
static_assert(sizeof(LEAD_SYNC) / sizeof(LEAD_SYNC[0]) % 2 == 0, "LEAD_SYNC must end on a space");
static_assert(sizeof(SPEED_UP) / sizeof(SPEED_UP[0]) % 2 == 0, "SPEED_UP must end on a space");
static_assert(sizeof(SPEED_DOWN) / sizeof(SPEED_DOWN[0]) % 2 == 0, "SPEED_DOWN must end on a space");
static_assert(sizeof(START) / sizeof(START[0]) % 2 == 0, "START must end on a space");
static_assert(sizeof(STOP) / sizeof(STOP[0]) % 2 == 0, "STOP must end on a space");

} // namespace rf_codes
