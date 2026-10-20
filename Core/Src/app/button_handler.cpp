#include "app/button_handler.hpp"

#include "app/board.hpp"
#include "app/config.hpp"
#include "app/serial.hpp"

namespace app {

void ButtonHandler::onEdge() {
  const uint32_t now = board::millis();
  if (board::buttonPressed()) {
    onPress(now);
  } else {
    onRelease(now);
  }
}

void ButtonHandler::onPress(uint32_t now) {
  if (pressed_ || (now - lastReleaseTick_) < config::kButtonDebounceMs) return;
  pressed_ = true;
  pressStartTick_ = now;
  if (pressIndex_ < kPressActuations) led_.toggle();
  sendLine("BUTTON ON");
}

void ButtonHandler::onRelease(uint32_t now) {
  if (!pressed_) return;
  if (pressIndex_ >= kPressActuations) led_.toggle();

  lastReleaseTick_ = now;
  const uint32_t duration = now - pressStartTick_;
  if (duration > config::kButtonLongPressMs) {
    const Measurement m = telemetry_.measure(board::adcCalibration());
    sendLine(Line("TEMP ") << m.temperatureC);
    sendLine(Line("VCC ") << m.vccMv);
  }
  sendLine("BUTTON OFF");
  sendLine(Line("BUTTON TIME ") << duration);

  pressed_ = false;
  pressIndex_ = static_cast<uint8_t>((pressIndex_ + 1U) % kCycleLength);
}

}  // namespace app
