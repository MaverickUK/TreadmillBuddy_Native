// Core 1: the treadmill controller. It owns the RF transmitter and works the
// belt towards core 0's target one remote press at a time (see belt.h),
// publishing what it believes the belt is doing. Core 1 runs nothing else,
// so its blocking waits (a 0.25 s RF burst, the gap between presses, the
// treadmill's START countdown) never hold up the screen or the buttons.
#pragma once

#include "treadmill_link.h"

namespace treadmill {

// The link between the cores (core 0 uses it too).
extern TreadmillLink link;

void core1Setup();
void core1Loop();

} // namespace treadmill
