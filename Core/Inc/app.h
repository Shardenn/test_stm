/**
  ******************************************************************************
  * @file           : app.h
  * @brief          : C interface of the C++ application (Core/Src/app/).
  *                   main.c and the HAL callbacks only forward to these.
  ******************************************************************************
  */
#ifndef APP_H
#define APP_H

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Stores the peripheral handles and initialises the board LEDs.
 *   samplingTimer - TIM2, its TRGO triggers the ADC (75 Hz)
 *   ledTimer      - TIM3, 1 kHz counter, its period is the LED phase
 *   adc           - ADC1, rank 1 temperature sensor, rank 2 VREFINT, DMA
 *   uart          - USART2, 115200 8E1 */
void App_Init(TIM_HandleTypeDef *samplingTimer, TIM_HandleTypeDef *ledTimer,
              ADC_HandleTypeDef *adc, UART_HandleTypeDef *uart);

/* Starts the timers, the ADC/DMA and the UART reception. */
void App_Start(void);

/* HAL callback forwarding. */
void App_OnTimerPeriodElapsed(TIM_HandleTypeDef *htim);
void App_OnGpioExti(uint16_t gpioPin);
void App_OnUartRxComplete(UART_HandleTypeDef *huart);

/* Error_Handler(): red LED on, interrupts off, never returns. */
void App_OnFatalError(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_H */
