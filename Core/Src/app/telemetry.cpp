#include "app/telemetry.hpp"

namespace app {
namespace {

// Cortex-M integer division semantics: division by zero yields 0 (the
// DIV_0_TRP trap is not enabled).  Keeps a not-yet-filled window harmless.
uint32_t divide(uint32_t n, uint32_t d) { return d == 0U ? 0U : n / d; }
int32_t divide(int32_t n, int32_t d) { return d == 0 ? 0 : n / d; }

}  // namespace

void Telemetry::addSample(uint16_t temperatureRaw, uint16_t vrefintRaw) {
  temperature_[next_] = temperatureRaw;
  vrefint_[next_] = vrefintRaw;
  next_ = (next_ + 1U) % kWindow;
}

Measurement Telemetry::measure(const board::AdcCalibration& cal) const {
  uint32_t temperatureSum = 0;
  uint32_t vrefintSum = 0;
  for (std::size_t i = 0; i < kWindow; ++i) {
    temperatureSum += temperature_[i];
    vrefintSum += vrefint_[i];
  }
  return convert(temperatureSum / kWindow, vrefintSum / kWindow, cal);
}

Measurement Telemetry::convert(uint32_t temperatureRaw, uint32_t vrefintRaw, const board::AdcCalibration& cal) {
  Measurement m;
  m.vccMv = divide(cal.vrefintCalMv * cal.vrefintRaw, vrefintRaw);
  const int32_t delta = static_cast<int32_t>(temperatureRaw) - static_cast<int32_t>(cal.tempCal1Raw);
  const int32_t span = static_cast<int32_t>(cal.tempCal2Raw) - static_cast<int32_t>(cal.tempCal1Raw);
  m.temperatureC = divide(delta * (cal.tempCal2C - cal.tempCal1C), span) + cal.tempCal1C;
  return m;
}

}  // namespace app
