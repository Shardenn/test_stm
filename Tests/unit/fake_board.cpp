#include "fake_board.hpp"

namespace fake {

BoardState& board() {
  static BoardState state;
  return state;
}

std::vector<std::string> takeLines() {
  std::vector<std::string> lines;
  std::string& s = board().serial;
  size_t pos = 0;
  for (size_t e; (e = s.find("\r\n", pos)) != std::string::npos; pos = e + 2) lines.push_back(s.substr(pos, e - pos));
  if (pos != s.size()) lines.push_back("<unterminated>" + s.substr(pos));
  s.clear();
  return lines;
}

}  // namespace fake

namespace app {
namespace board {

uint32_t millis() { return fake::board().tick; }
void ledToggle() { ++fake::board().ledToggles; }
void ledTimerRestart(uint32_t autoReload) { fake::board().timerReloads.push_back(autoReload); }
bool buttonPressed() { return fake::board().button; }
void serialWrite(const char* data, std::size_t length) { fake::board().serial.append(data, length); }
AdcCalibration adcCalibration() { return fake::board().calibration; }

}  // namespace board
}  // namespace app
