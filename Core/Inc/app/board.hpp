// Hardware access used by the application classes.
//
// Implemented on top of the HAL/BSP in board_stm32.cpp; the host unit tests
// link a fake implementation instead.
#pragma once

#include <cstddef>
#include <cstdint>

namespace app {
namespace board {

// Factory calibration of the internal temperature sensor and VREFINT.
struct AdcCalibration {
  uint32_t vrefintCalMv;  // VDDA at which VREFINT was calibrated
  uint16_t vrefintRaw;    // VREFINT reading at that VDDA
  int32_t tempCal1C;      // first calibration temperature ...
  uint16_t tempCal1Raw;   // ... and the sensor reading there
  int32_t tempCal2C;
  uint16_t tempCal2Raw;
};

uint32_t millis();                          // HAL tick
void ledToggle();                           // the blinking LED (LD4, green)
void ledTimerRestart(uint32_t autoReload);  // new period, count from zero
bool buttonPressed();                       // user button B1
void serialWrite(const char* data, std::size_t length);  // blocking
AdcCalibration adcCalibration();

}  // namespace board
}  // namespace app
