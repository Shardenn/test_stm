// Top-level application: owns the components and receives the hardware
// events (all of them from interrupt context).
#pragma once

#include <cstdint>

#include "app/button_handler.hpp"
#include "app/command_parser.hpp"
#include "app/led_blinker.hpp"
#include "app/telemetry.hpp"

namespace app {

class Application {
 public:
  constexpr Application() : button_(led_, telemetry_) {}

  void onLedTimer() { led_.toggle(); }
  void onSamplingTimer(uint16_t temperatureRaw, uint16_t vrefintRaw) { telemetry_.addSample(temperatureRaw, vrefintRaw); }
  void onButtonEdge() { button_.onEdge(); }
  void onSerialByte(uint8_t byte);

 private:
  void execute(const Command& command);

  LedBlinker led_;
  Telemetry telemetry_;
  ButtonHandler button_;
  CommandParser parser_;
};

}  // namespace app
