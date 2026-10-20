#pragma once

#include <cstdint>

// Boots the firmware, drives it with random stimuli derived from `seed` and
// prints the emulator trace to stdout.
void runFuzzScenario(uint32_t seed);
