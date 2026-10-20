// The blinking LED: alternates ON/OFF phases timed by TIM3.
#pragma once

#include <cstddef>
#include <cstdint>

#include "app/config.hpp"

namespace app {

enum class LedState : uint8_t { Off = 0, On = 1 };

class LedBlinker {
 public:
  constexpr LedBlinker() = default;

  // Switches to the other state - on the LED timer's period end or on a
  // button actuation.  Reports the length of the finished phase and the new
  // state, and restarts the timer with the new state's period.
  void toggle();

  LedState state() const { return state_; }

  // Period (TIM3 reload value) of a state.  A change takes effect the next
  // time the LED enters that state.
  uint32_t period(LedState s) const { return periods_[index(s)]; }
  void setPeriod(LedState s, uint32_t autoReload) { periods_[index(s)] = autoReload; }

 private:
  static constexpr std::size_t index(LedState s) { return static_cast<std::size_t>(s); }

  uint32_t periods_[2] = {config::kLedOffPeriodDefault, config::kLedOnPeriodDefault};
  LedState state_ = LedState::Off;
  uint32_t stateStartTick_ = 0;
};

}  // namespace app
