// User button: toggles the LED on "actuations" and reports its state.
//
// Actuations follow a repeating 4-press cycle: the press of the 1st and 2nd
// press, then the release of the 3rd and 4th press.
#pragma once

#include <cstdint>

#include "app/led_blinker.hpp"
#include "app/telemetry.hpp"

namespace app {

class ButtonHandler {
 public:
  constexpr ButtonHandler(LedBlinker& led, const Telemetry& telemetry) : led_(led), telemetry_(telemetry) {}

  // Called on every edge of the button input (EXTI, both edges).
  void onEdge();

 private:
  static constexpr uint8_t kCycleLength = 4;
  static constexpr uint8_t kPressActuations = 2;  // first two presses of the cycle act on press

  void onPress(uint32_t now);
  void onRelease(uint32_t now);

  LedBlinker& led_;
  const Telemetry& telemetry_;
  uint32_t pressStartTick_ = 0;
  uint32_t lastReleaseTick_ = 0;
  uint8_t pressIndex_ = 0;  // position within the cycle
  bool pressed_ = false;
};

}  // namespace app
