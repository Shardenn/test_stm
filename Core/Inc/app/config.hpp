// Application parameters.
#pragma once

#include <cstddef>
#include <cstdint>

namespace app {
namespace config {

// LED phase lengths are kept as TIM3 auto-reload values: TIM3 counts at
// 1 kHz, so a phase lasts (value + 1) ms.  The serial protocol reads and
// writes these values directly.
constexpr uint32_t kLedOnPeriodDefault = 3999U;
constexpr uint32_t kLedOffPeriodDefault = 5999U;
constexpr uint32_t kLedPeriodMin = 500U;
constexpr uint32_t kLedPeriodMax = 9999U;

// A press is ignored if it follows the previous release within this time.
constexpr uint32_t kButtonDebounceMs = 20U;
// Releasing after a press longer than this prints temperature and VCC.
constexpr uint32_t kButtonLongPressMs = 500U;

// Number of ADC samples averaged for temperature and VCC.
constexpr std::size_t kAdcAverageWindow = 10U;

}  // namespace config
}  // namespace app
