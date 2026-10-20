/*
 * Host-side replacement for the STM32F4 HAL umbrella header.
 *
 * It declares exactly the subset of HAL/CMSIS types, constants, macros and
 * functions that the firmware (Core/Src/main.c, the C++ application and the
 * real Core/Inc/main.h + BSP header) uses.  The functions are implemented by
 * the board emulator (Tests/emulator/emulator.cpp), which models the
 * peripherals of the STM32F411E-DISCO closely enough to run the firmware.
 *
 * Numeric values of configuration constants are copied from the real HAL so
 * the emulator can derive clocks/timings from the very same Init structures
 * the firmware fills in.
 */
#ifndef EMU_STM32F4XX_HAL_H
#define EMU_STM32F4XX_HAL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ CMSIS */
typedef enum { RESET = 0U, SET = !RESET } FlagStatus, ITStatus;
typedef enum { DISABLE = 0U, ENABLE = !DISABLE } FunctionalState;

typedef enum {
  NonMaskableInt_IRQn = -14,
  SysTick_IRQn        = -1,
  EXTI0_IRQn          = 6,
  ADC_IRQn            = 18,
  TIM2_IRQn           = 28,
  TIM3_IRQn           = 29,
  USART2_IRQn         = 38,
  DMA2_Stream0_IRQn   = 56
} IRQn_Type;

void emu_disable_irq(void);
void emu_nop(void);
#define __disable_irq() emu_disable_irq()
#define __NOP()         emu_nop()

/* Peripheral register blocks: only the registers the firmware touches. */
typedef struct { volatile uint32_t CR1, CR2, DIER, SR, EGR, CNT, PSC, ARR; } TIM_TypeDef;
typedef struct { volatile uint32_t SR, DR, BRR, CR1, CR2, CR3; } USART_TypeDef;
typedef struct { volatile uint32_t IDR, ODR; } GPIO_TypeDef;
typedef struct { volatile uint32_t SR, CR1, CR2; } ADC_TypeDef;
typedef struct { volatile uint32_t DR; } CRC_TypeDef;
typedef struct { volatile uint32_t CR1; } I2C_TypeDef;
typedef struct { volatile uint32_t CR1; } SPI_TypeDef;

extern TIM_TypeDef   emu_TIM2, emu_TIM3;
extern USART_TypeDef emu_USART2;
extern GPIO_TypeDef  emu_GPIOA, emu_GPIOB, emu_GPIOC, emu_GPIOD, emu_GPIOE, emu_GPIOH;
extern ADC_TypeDef   emu_ADC1;
extern CRC_TypeDef   emu_CRC;
extern I2C_TypeDef   emu_I2C1;
extern SPI_TypeDef   emu_SPI1, emu_SPI3;

#define TIM2   (&emu_TIM2)
#define TIM3   (&emu_TIM3)
#define USART2 (&emu_USART2)
#define GPIOA  (&emu_GPIOA)
#define GPIOB  (&emu_GPIOB)
#define GPIOC  (&emu_GPIOC)
#define GPIOD  (&emu_GPIOD)
#define GPIOE  (&emu_GPIOE)
#define GPIOH  (&emu_GPIOH)
#define ADC1   (&emu_ADC1)
#define CRC    (&emu_CRC)
#define I2C1   (&emu_I2C1)
#define SPI1   (&emu_SPI1)
#define SPI3   (&emu_SPI3)

/* ------------------------------------------------------------------- HAL */
typedef enum { HAL_OK = 0x00U, HAL_ERROR = 0x01U, HAL_BUSY = 0x02U, HAL_TIMEOUT = 0x03U } HAL_StatusTypeDef;
#define HAL_MAX_DELAY 0xFFFFFFFFU

HAL_StatusTypeDef HAL_Init(void);
uint32_t HAL_GetTick(void);
void HAL_IncTick(void);
void HAL_NVIC_SetPriority(IRQn_Type IRQn, uint32_t PreemptPriority, uint32_t SubPriority);
void HAL_NVIC_EnableIRQ(IRQn_Type IRQn);

/* ---- Factory calibration values (system memory on the real chip) ---- */
extern uint16_t emu_vrefint_cal, emu_ts_cal1, emu_ts_cal2;
#define VREFINT_CAL_ADDR      (&emu_vrefint_cal)
#define VREFINT_CAL_VREF      ( 3300UL)
#define TEMPSENSOR_CAL1_ADDR  (&emu_ts_cal1)
#define TEMPSENSOR_CAL2_ADDR  (&emu_ts_cal2)
#define TEMPSENSOR_CAL1_TEMP  (( int32_t)   30)
#define TEMPSENSOR_CAL2_TEMP  (( int32_t)  110)

/* ------------------------------------------------------------------- RCC */
typedef struct {
  uint32_t PLLState, PLLSource, PLLM, PLLN, PLLP, PLLQ;
} RCC_PLLInitTypeDef;
typedef struct {
  uint32_t OscillatorType, HSEState, LSEState, HSIState, HSICalibrationValue, LSIState;
  RCC_PLLInitTypeDef PLL;
} RCC_OscInitTypeDef;
typedef struct {
  uint32_t ClockType, SYSCLKSource, AHBCLKDivider, APB1CLKDivider, APB2CLKDivider;
} RCC_ClkInitTypeDef;

#define RCC_OSCILLATORTYPE_HSE   0x00000001U
#define RCC_HSE_ON               0x00010000U
#define RCC_PLL_ON               ((uint8_t)0x02)
#define RCC_PLLSOURCE_HSE        0x00400000U
#define RCC_PLLP_DIV2            0x00000002U
#define RCC_PLLP_DIV4            0x00000004U
#define RCC_PLLP_DIV6            0x00000006U
#define RCC_PLLP_DIV8            0x00000008U
#define RCC_CLOCKTYPE_SYSCLK     0x00000001U
#define RCC_CLOCKTYPE_HCLK       0x00000002U
#define RCC_CLOCKTYPE_PCLK1      0x00000004U
#define RCC_CLOCKTYPE_PCLK2      0x00000008U
#define RCC_SYSCLKSOURCE_PLLCLK  0x00000002U
#define RCC_SYSCLK_DIV1          0x00000000U
#define RCC_HCLK_DIV1            0x00000000U
#define RCC_HCLK_DIV2            0x00001000U
#define RCC_HCLK_DIV4            0x00001400U
#define RCC_HCLK_DIV8            0x00001800U
#define RCC_HCLK_DIV16           0x00001C00U
#define FLASH_LATENCY_3          0x00000003U
#define PWR_REGULATOR_VOLTAGE_SCALE1 0x0000C000U

HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *RCC_OscInitStruct);
HAL_StatusTypeDef HAL_RCC_ClockConfig(RCC_ClkInitTypeDef *RCC_ClkInitStruct, uint32_t FLatency);

#define __HAL_RCC_PWR_CLK_ENABLE()    do { } while (0)
#define __HAL_RCC_DMA2_CLK_ENABLE()   do { } while (0)
#define __HAL_RCC_GPIOA_CLK_ENABLE()  do { } while (0)
#define __HAL_RCC_GPIOB_CLK_ENABLE()  do { } while (0)
#define __HAL_RCC_GPIOC_CLK_ENABLE()  do { } while (0)
#define __HAL_RCC_GPIOD_CLK_ENABLE()  do { } while (0)
#define __HAL_RCC_GPIOE_CLK_ENABLE()  do { } while (0)
#define __HAL_RCC_GPIOH_CLK_ENABLE()  do { } while (0)
#define __HAL_PWR_VOLTAGESCALING_CONFIG(__REGULATOR__) do { (void)(__REGULATOR__); } while (0)

/* ------------------------------------------------------------------ GPIO */
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET } GPIO_PinState;
typedef struct { uint32_t Pin, Mode, Pull, Speed, Alternate; } GPIO_InitTypeDef;

#define GPIO_PIN_0   ((uint16_t)0x0001)
#define GPIO_PIN_1   ((uint16_t)0x0002)
#define GPIO_PIN_2   ((uint16_t)0x0004)
#define GPIO_PIN_3   ((uint16_t)0x0008)
#define GPIO_PIN_4   ((uint16_t)0x0010)
#define GPIO_PIN_5   ((uint16_t)0x0020)
#define GPIO_PIN_6   ((uint16_t)0x0040)
#define GPIO_PIN_7   ((uint16_t)0x0080)
#define GPIO_PIN_8   ((uint16_t)0x0100)
#define GPIO_PIN_9   ((uint16_t)0x0200)
#define GPIO_PIN_10  ((uint16_t)0x0400)
#define GPIO_PIN_11  ((uint16_t)0x0800)
#define GPIO_PIN_12  ((uint16_t)0x1000)
#define GPIO_PIN_13  ((uint16_t)0x2000)
#define GPIO_PIN_14  ((uint16_t)0x4000)
#define GPIO_PIN_15  ((uint16_t)0x8000)

#define GPIO_MODE_INPUT              0x00000000U
#define GPIO_MODE_OUTPUT_PP          0x00000001U
#define GPIO_MODE_AF_PP              0x00000002U
#define GPIO_MODE_IT_RISING_FALLING  0x10310000U
#define GPIO_MODE_EVT_RISING         0x10120000U
#define GPIO_NOPULL                  0x00000000U
#define GPIO_PULLDOWN                0x00000002U
#define GPIO_SPEED_FREQ_LOW          0x00000000U
#define GPIO_SPEED_FREQ_HIGH         0x00000002U
#define GPIO_SPEED_FREQ_VERY_HIGH    0x00000003U
#define GPIO_AF4_I2C1                ((uint8_t)0x04)
#define GPIO_AF5_SPI1                ((uint8_t)0x05)
#define GPIO_AF5_SPI2                ((uint8_t)0x05)
#define GPIO_AF10_OTG_FS             ((uint8_t)0x0A)

void HAL_GPIO_Init(GPIO_TypeDef *GPIOx, GPIO_InitTypeDef *GPIO_Init);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);
void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState);
void HAL_GPIO_TogglePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);
void HAL_GPIO_EXTI_IRQHandler(uint16_t GPIO_Pin);
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin);

/* ------------------------------------------------------------------- DMA */
typedef struct { void *Instance; void *Parent; } DMA_HandleTypeDef;

/* ------------------------------------------------------------------- ADC */
typedef struct {
  uint32_t ClockPrescaler, Resolution, DataAlign, ScanConvMode, EOCSelection;
  FunctionalState ContinuousConvMode;
  uint32_t NbrOfConversion;
  FunctionalState DiscontinuousConvMode;
  uint32_t NbrOfDiscConversion, ExternalTrigConv, ExternalTrigConvEdge;
  FunctionalState DMAContinuousRequests;
} ADC_InitTypeDef;
typedef struct {
  ADC_TypeDef *Instance;
  ADC_InitTypeDef Init;
  DMA_HandleTypeDef *DMA_Handle;
} ADC_HandleTypeDef;
typedef struct { uint32_t Channel, Rank, SamplingTime, Offset; } ADC_ChannelConfTypeDef;

#define ADC_CLOCK_SYNC_PCLK_DIV2          0x00000000U
#define ADC_CLOCK_SYNC_PCLK_DIV4          0x00010000U
#define ADC_CLOCK_SYNC_PCLK_DIV6          0x00020000U
#define ADC_CLOCK_SYNC_PCLK_DIV8          0x00030000U
#define ADC_RESOLUTION_12B                0x00000000U
#define ADC_DATAALIGN_RIGHT               0x00000000U
#define ADC_EOC_SINGLE_CONV               0x00000001U
#define ADC_EXTERNALTRIGCONVEDGE_RISING   0x10000000U
#define ADC_EXTERNALTRIGCONV_T2_TRGO      0x06000000U
#define ADC_CHANNEL_VREFINT               17U
#define ADC_CHANNEL_TEMPSENSOR            18U   /* STM32F411: ADC_IN18 */
#define ADC_SAMPLETIME_3CYCLES            0U
#define ADC_SAMPLETIME_480CYCLES          7U

HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef *hadc);
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *hadc, ADC_ChannelConfTypeDef *sConfig);
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *hadc, uint32_t *pData, uint32_t Length);

/* ------------------------------------------------------------------- CRC */
typedef struct { CRC_TypeDef *Instance; } CRC_HandleTypeDef;
HAL_StatusTypeDef HAL_CRC_Init(CRC_HandleTypeDef *hcrc);
#define __HAL_CRC_DR_RESET(__HANDLE__) do { (__HANDLE__)->Instance->DR = 0xFFFFFFFFU; } while (0)

/* ------------------------------------------------------------------- I2C */
typedef struct {
  uint32_t ClockSpeed, DutyCycle, OwnAddress1, AddressingMode, DualAddressMode,
           OwnAddress2, GeneralCallMode, NoStretchMode;
} I2C_InitTypeDef;
typedef struct { I2C_TypeDef *Instance; I2C_InitTypeDef Init; } I2C_HandleTypeDef;
#define I2C_DUTYCYCLE_2             0x00000000U
#define I2C_ADDRESSINGMODE_7BIT     0x00004000U
#define I2C_DUALADDRESS_DISABLE     0x00000000U
#define I2C_GENERALCALL_DISABLE     0x00000000U
#define I2C_NOSTRETCH_DISABLE       0x00000000U
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *hi2c);

/* ------------------------------------------------------------------- I2S */
typedef struct {
  uint32_t Mode, Standard, DataFormat, MCLKOutput, AudioFreq, CPOL, ClockSource, FullDuplexMode;
} I2S_InitTypeDef;
typedef struct { SPI_TypeDef *Instance; I2S_InitTypeDef Init; } I2S_HandleTypeDef;
#define I2S_MODE_MASTER_TX          0x00000200U
#define I2S_STANDARD_PHILIPS        0x00000000U
#define I2S_DATAFORMAT_16B          0x00000000U
#define I2S_MCLKOUTPUT_DISABLE      0x00000000U
#define I2S_AUDIOFREQ_16K           16000U
#define I2S_CPOL_LOW                0x00000000U
#define I2S_CLOCK_PLL               0x00000000U
#define I2S_FULLDUPLEXMODE_ENABLE   0x00000001U
HAL_StatusTypeDef HAL_I2S_Init(I2S_HandleTypeDef *hi2s);

/* ------------------------------------------------------------------- SPI */
typedef struct {
  uint32_t Mode, Direction, DataSize, CLKPolarity, CLKPhase, NSS, BaudRatePrescaler,
           FirstBit, TIMode, CRCCalculation, CRCPolynomial;
} SPI_InitTypeDef;
typedef struct { SPI_TypeDef *Instance; SPI_InitTypeDef Init; } SPI_HandleTypeDef;
#define SPI_MODE_MASTER              0x00000104U
#define SPI_DIRECTION_1LINE          0x00008000U
#define SPI_DATASIZE_8BIT            0x00000000U
#define SPI_POLARITY_LOW             0x00000000U
#define SPI_PHASE_1EDGE              0x00000000U
#define SPI_NSS_SOFT                 0x00000200U
#define SPI_BAUDRATEPRESCALER_2      0x00000000U
#define SPI_FIRSTBIT_MSB             0x00000000U
#define SPI_TIMODE_DISABLE           0x00000000U
#define SPI_CRCCALCULATION_DISABLE   0x00000000U
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *hspi);

/* ------------------------------------------------------------------- TIM */
typedef struct {
  uint32_t Prescaler, CounterMode, Period, ClockDivision, RepetitionCounter, AutoReloadPreload;
} TIM_Base_InitTypeDef;
typedef struct {
  TIM_TypeDef *Instance;
  TIM_Base_InitTypeDef Init;
} TIM_HandleTypeDef;
typedef struct { uint32_t ClockSource, ClockPolarity, ClockPrescaler, ClockFilter; } TIM_ClockConfigTypeDef;
typedef struct { uint32_t MasterOutputTrigger, MasterSlaveMode; } TIM_MasterConfigTypeDef;

#define TIM_COUNTERMODE_UP               0x00000000U
#define TIM_CLOCKDIVISION_DIV1           0x00000000U
#define TIM_AUTORELOAD_PRELOAD_DISABLE   0x00000000U
#define TIM_CLOCKSOURCE_INTERNAL         0x00001000U
#define TIM_TRGO_RESET                   0x00000000U
#define TIM_TRGO_UPDATE                  0x00000020U
#define TIM_MASTERSLAVEMODE_DISABLE      0x00000000U

HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_TIM_ConfigClockSource(TIM_HandleTypeDef *htim, TIM_ClockConfigTypeDef *sClockSourceConfig);
HAL_StatusTypeDef HAL_TIMEx_MasterConfigSynchronization(TIM_HandleTypeDef *htim, TIM_MasterConfigTypeDef *sMasterConfig);
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *htim);
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim);

/* Same definitions as the real HAL (stm32f4xx_hal_tim.h). */
#define __HAL_TIM_SET_COUNTER(__HANDLE__, __COUNTER__)  ((__HANDLE__)->Instance->CNT = (__COUNTER__))
#define __HAL_TIM_SET_AUTORELOAD(__HANDLE__, __AUTORELOAD__) \
  do{                                                    \
    (__HANDLE__)->Instance->ARR = (__AUTORELOAD__);  \
    (__HANDLE__)->Init.Period = (__AUTORELOAD__);    \
  } while(0)

/* ------------------------------------------------------------------ UART */
typedef struct {
  uint32_t BaudRate, WordLength, StopBits, Parity, Mode, HwFlowCtl, OverSampling;
} UART_InitTypeDef;
typedef struct {
  USART_TypeDef *Instance;
  UART_InitTypeDef Init;
} UART_HandleTypeDef;

#define UART_WORDLENGTH_8B      0x00000000U
#define UART_WORDLENGTH_9B      0x00001000U
#define UART_STOPBITS_1         0x00000000U
#define UART_STOPBITS_2         0x00002000U
#define UART_PARITY_NONE        0x00000000U
#define UART_PARITY_EVEN        0x00000400U
#define UART_PARITY_ODD         0x00000600U
#define UART_MODE_TX_RX         0x0000000CU
#define UART_HWCONTROL_NONE     0x00000000U
#define UART_OVERSAMPLING_16    0x00000000U

HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *huart);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, const uint8_t *pData, uint16_t Size, uint32_t Timeout);
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size);
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif /* EMU_STM32F4XX_HAL_H */
