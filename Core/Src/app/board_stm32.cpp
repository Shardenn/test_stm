#include "app/board_stm32.hpp"

#include "app/board.hpp"

namespace app {
namespace board {
namespace {

TIM_HandleTypeDef* g_ledTimer = nullptr;
UART_HandleTypeDef* g_uart = nullptr;

}  // namespace

void attach(TIM_HandleTypeDef* ledTimer, UART_HandleTypeDef* uart) {
  g_ledTimer = ledTimer;
  g_uart = uart;
}

uint32_t millis() { return HAL_GetTick(); }

void ledToggle() { BSP_LED_Toggle(LED4); }

void ledTimerRestart(uint32_t autoReload) {
  __HAL_TIM_SET_AUTORELOAD(g_ledTimer, autoReload);
  __HAL_TIM_SET_COUNTER(g_ledTimer, 0);
}

bool buttonPressed() { return BSP_PB_GetState(BUTTON_KEY) == SET; }

void serialWrite(const char* data, std::size_t length) {
  HAL_UART_Transmit(g_uart, reinterpret_cast<const uint8_t*>(data), static_cast<uint16_t>(length), HAL_MAX_DELAY);
}

AdcCalibration adcCalibration() {
  return {VREFINT_CAL_VREF,     *VREFINT_CAL_ADDR,     TEMPSENSOR_CAL1_TEMP,
          *TEMPSENSOR_CAL1_ADDR, TEMPSENSOR_CAL2_TEMP, *TEMPSENSOR_CAL2_ADDR};
}

}  // namespace board
}  // namespace app
