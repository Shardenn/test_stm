// Recording fake of app::board for the unit tests.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app/board.hpp"

namespace fake {

struct BoardState {
  uint32_t tick = 0;
  bool button = false;
  int ledToggles = 0;
  std::vector<uint32_t> timerReloads;
  std::string serial;
  app::board::AdcCalibration calibration = {3300, 1520, 30, 940, 110, 1210};
};

BoardState& board();

// Serial output split into lines (without "\r\n"); clears the buffer.
std::vector<std::string> takeLines();

}  // namespace fake
