#include "app/led_blinker.hpp"

#include "app/board.hpp"
#include "app/serial.hpp"

namespace app {

void LedBlinker::toggle() {
  const uint32_t now = board::millis();
  const uint32_t elapsed = now - stateStartTick_;
  sendLine(Line(state_ == LedState::On ? "LED ON TIME " : "LED OFF TIME ") << elapsed);

  stateStartTick_ = now;
  board::ledToggle();
  state_ = state_ == LedState::On ? LedState::Off : LedState::On;
  board::ledTimerRestart(period(state_));
  sendLine(state_ == LedState::On ? "LED ON" : "LED OFF");
}

}  // namespace app
