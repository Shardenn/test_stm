// C entry points (app.h) for main.c and the HAL callbacks.
#include "app.h"

#include "app/application.hpp"
#include "app/board_stm32.hpp"

namespace {

app::Application g_application;

TIM_HandleTypeDef* g_samplingTimer = nullptr;
TIM_HandleTypeDef* g_ledTimer = nullptr;
ADC_HandleTypeDef* g_adc = nullptr;
UART_HandleTypeDef* g_uart = nullptr;

// Written by DMA after every ADC scan: [0] temperature sensor, [1] VREFINT.
volatile uint16_t g_adcDma[2];
constexpr uint32_t kAdcChannels = 2;

uint8_t g_rxByte;

}  // namespace

extern "C" void App_Init(TIM_HandleTypeDef* samplingTimer, TIM_HandleTypeDef* ledTimer, ADC_HandleTypeDef* adc,
                         UART_HandleTypeDef* uart) {
  g_samplingTimer = samplingTimer;
  g_ledTimer = ledTimer;
  g_adc = adc;
  g_uart = uart;
  app::board::attach(ledTimer, uart);

  BSP_LED_Init(LED3);
  BSP_LED_Init(LED4);
  BSP_LED_Init(LED5);
  BSP_LED_Init(LED6);
}

extern "C" void App_Start(void) {
  HAL_TIM_Base_Start_IT(g_samplingTimer);
  HAL_TIM_Base_Start_IT(g_ledTimer);
  HAL_ADC_Start_DMA(g_adc, reinterpret_cast<uint32_t*>(const_cast<uint16_t*>(g_adcDma)), kAdcChannels);
  HAL_UART_Receive_IT(g_uart, &g_rxByte, 1);
}

extern "C" void App_OnTimerPeriodElapsed(TIM_HandleTypeDef* htim) {
  if (htim->Instance == g_ledTimer->Instance) {
    g_application.onLedTimer();
  } else if (htim->Instance == g_samplingTimer->Instance) {
    // The update event that raised this interrupt also triggers the next ADC
    // scan, so this stores the result of the previous scan.
    g_application.onSamplingTimer(g_adcDma[0], g_adcDma[1]);
  }
}

extern "C" void App_OnGpioExti(uint16_t gpioPin) {
  if (gpioPin == KEY_BUTTON_PIN) g_application.onButtonEdge();
}

extern "C" void App_OnUartRxComplete(UART_HandleTypeDef* huart) {
  if (huart->Instance != g_uart->Instance) return;
  g_application.onSerialByte(g_rxByte);
  HAL_UART_Receive_IT(g_uart, &g_rxByte, 1);
}

extern "C" void App_OnFatalError(void) {
  BSP_LED_On(LED5);
  __disable_irq();
  for (;;) {
    __NOP();
  }
}
