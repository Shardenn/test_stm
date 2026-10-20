// Moving average of the temperature sensor and VREFINT readings and their
// conversion to degrees Celsius and millivolts.
#pragma once

#include <cstddef>
#include <cstdint>

#include "app/board.hpp"
#include "app/config.hpp"

namespace app {

struct Measurement {
  int32_t temperatureC;
  uint32_t vccMv;
};

class Telemetry {
 public:
  static constexpr std::size_t kWindow = config::kAdcAverageWindow;

  constexpr Telemetry() = default;

  // One sample per sampling-timer period; the oldest one is replaced.
  void addSample(uint16_t temperatureRaw, uint16_t vrefintRaw);

  // Average of the last kWindow samples (missing samples count as 0).
  Measurement measure(const board::AdcCalibration& cal) const;

  // VCC from VREFINT, temperature by linear interpolation between the two
  // calibration points.  Integer arithmetic, truncated like the C division.
  static Measurement convert(uint32_t temperatureRaw, uint32_t vrefintRaw, const board::AdcCalibration& cal);

 private:
  uint16_t temperature_[kWindow] = {};
  uint16_t vrefint_[kWindow] = {};
  std::size_t next_ = 0;
};

}  // namespace app
