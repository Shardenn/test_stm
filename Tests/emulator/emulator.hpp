// Cycle-approximate emulator of the STM32F411E-DISCO board, as far as the
// firmware under test uses it.
//
// The firmware (unmodified Core/Src/main.c + application code) is compiled
// for the host against the fake HAL in emulator/include and linked with this
// emulator, which implements the HAL functions and models the hardware:
//
//   * RCC        - SYSCLK/HCLK/PCLK1/PCLK2/timer clocks derived from the
//                  RCC_OscInit/ClkInit structures the firmware passes in.
//   * SysTick    - 1 ms tick driving HAL_GetTick().
//   * TIM2/TIM3  - up-counters with prescaler, ARR, CNT, update interrupt and
//                  TRGO (TIM2 update -> ADC trigger).
//   * ADC1 + DMA - scan of the configured channels on each trigger, results
//                  written by "DMA" (circular, half-word) after the real
//                  conversion time; the analog inputs are scenario functions.
//   * EXTI0/PA0  - the user button (both edges), with arbitrary bounce.
//   * USART2     - frame time derived from baud/word length/stop bits;
//                  blocking HAL_UART_Transmit consumes CPU time; reception
//                  through HAL_UART_Receive_IT incl. overrun (ORE) handling
//                  exactly like HAL_UART_IRQHandler does it.
//   * NVIC       - all firmware IRQs have priority 0 (see stm32f4xx_hal_msp.c
//                  and TICK_INT_PRIORITY), so ISRs never nest: they run to
//                  completion and pending ones are taken in exception-number
//                  order.  While an ISR runs (e.g. blocked in a UART transmit)
//                  hardware keeps going and interrupts only become pending; a
//                  source that is already pending loses the extra event
//                  (this is how SysTick ticks get lost on the real chip).
//
// Time is kept in picoseconds.  Firmware code other than UART transmission is
// assumed to take zero time.
#pragma once

#include "stm32f4xx_hal.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace emu {

using Time = uint64_t;  // picoseconds

constexpr Time ps(uint64_t v) { return v; }
constexpr Time ns(uint64_t v) { return v * 1000ULL; }
constexpr Time us(uint64_t v) { return v * 1000000ULL; }
constexpr Time ms(uint64_t v) { return v * 1000000000ULL; }
constexpr double toMs(Time t) { return static_cast<double>(t) / 1e9; }

enum class Led { Led4 = 0, Led3 = 1, Led5 = 2, Led6 = 3 };  // PD12..PD15 (green, orange, red, blue)

struct TxByte {
  Time end;  // when the stop bit finished
  char c;
};

struct TxLine {
  Time start;        // when the first byte started shifting out
  std::string text;  // without the terminating "\r\n"
};

struct LedEdge {
  Time t;
  Led led;
  bool on;
};

struct AdcConversion {
  Time t;
  uint32_t channel;
  uint16_t value;
};

struct DmaSnapshot {
  Time t;                      // when the TIM2 ISR was entered
  std::vector<uint16_t> data;  // DMA buffer content the ISR saw
};

struct Config {
  // Factory calibration words (real chips: VREFINT_CAL ~1500..1530,
  // TS_CAL1 ~940, TS_CAL2 ~1200).
  uint16_t vrefintCal = 1520;
  uint16_t tsCal1 = 940;
  uint16_t tsCal2 = 1210;
  // Time at which the firmware finished its initialisation (HSE/PLL start-up etc.).
  Time bootTime = us(2500);
  // Analog inputs, sampled at the end of each conversion.
  std::function<uint16_t(Time)> tempRaw = [](Time) -> uint16_t { return 1060; };
  std::function<uint16_t(Time)> vrefRaw = [](Time) -> uint16_t { return 1700; };
};

struct ClockTree {
  uint32_t sysclk = 16000000;  // HSI after reset
  uint32_t hclk = 16000000;
  uint32_t pclk1 = 16000000;
  uint32_t pclk2 = 16000000;
  uint32_t apb1TimerClk = 16000000;
};

class Board {
 public:
  static Board& instance();

  // ---- set-up ---------------------------------------------------------
  Config& config() { return config_; }
  // Runs the firmware's main() on its own thread and returns once the
  // firmware has started all peripherals and sits in its main loop.
  void boot();

  // ---- simulation -----------------------------------------------------
  Time now() const { return now_; }
  void runUntil(Time t);
  void runFor(Time dt) { runUntil(now_ + dt); }

  // ---- stimuli --------------------------------------------------------
  void setButton(bool pressed) { scheduleButton(now_, pressed); runUntil(now_); }
  void scheduleButton(Time t, bool pressed);
  // Host -> MCU bytes, sent back-to-back starting at `t` (or right after
  // bytes that are still queued, whichever is later).
  void uartSendAt(Time t, const std::string& bytes);
  void uartSend(const std::string& bytes) { uartSendAt(now_, bytes); }

  // ---- observations ---------------------------------------------------
  const ClockTree& clocks() const { return clocks_; }
  const std::vector<TxByte>& txBytes() const { return tx_; }
  std::string txText(Time from = 0) const;
  std::vector<TxLine> txLines(Time from = 0) const;  // complete "\r\n"-terminated lines only
  bool ledOn(Led led) const;
  const std::vector<LedEdge>& ledEdges() const { return ledEdges_; }
  std::vector<LedEdge> ledEdges(Led led, Time from = 0) const;
  const std::vector<AdcConversion>& adcConversions() const { return adcConversions_; }
  // Copies of the DMA buffer taken each time the TIM2 interrupt was entered.
  const std::vector<DmaSnapshot>& tim2IsrDmaSnapshots() const { return tim2Snapshots_; }
  uint32_t halTick() const { return uwTick_; }
  uint32_t lostSysTicks() const { return lostSysTicks_; }
  uint32_t uartOverruns() const { return uartOverruns_; }
  bool uartRxArmed() const { return uart_.rxArmed; }
  bool fatalErrorEntered() const { return fatal_; }
  Time uartFrameTime() const { return uart_.frameTime; }
  Time timerTickPeriod(int tim) const { return tim == 2 ? tim2_.tickPeriod : tim3_.tickPeriod; }
  const TIM_HandleTypeDef* timerHandle(int tim) const;
  const UART_HandleTypeDef* uartHandle() const { return uart_.handle; }
  // Full deterministic trace (TX lines with timestamps, LED edges, ...).
  std::string trace() const;

  // ---- Internal state -------------------------------------------------
  // Public only so that the C HAL stubs in emulator.cpp can drive it; tests
  // should use the API above.

  enum Irq { kSysTick, kExti0, kTim2, kTim3, kUsart2, kIrqCount };  // exception-number order

  struct Timer {
    int id = 0;
    TIM_HandleTypeDef* handle = nullptr;
    bool running = false;
    bool trgoOnUpdate = false;
    Time tickPeriod = 0;
    Time nextTick = 0;
    uint32_t maxCount = 0xFFFF;
    Irq irq = kTim2;
  };

  struct Uart {
    UART_HandleTypeDef* handle = nullptr;
    Time frameTime = 0;
    bool rxArmed = false;
    uint8_t* rxPtr = nullptr;
    bool rxne = false;
    bool ore = false;
    uint8_t rdr = 0;
    std::deque<std::pair<Time, uint8_t>> rxQueue;
  };

  struct Adc {
    ADC_HandleTypeDef* handle = nullptr;
    std::vector<uint32_t> ranks;  // channel per rank
    Time conversionTime = 0;      // per channel
    uint16_t* dmaBuffer = nullptr;
    uint32_t dmaLength = 0;
    uint32_t dmaIndex = 0;
    bool running = false;
    bool triggeredByTim2 = false;
    std::deque<std::pair<Time, uint32_t>> pending;  // (completion time, rank index)
  };

  Time nextEventTime() const;
  void processNextEvent();
  void advanceHardware(Time dt);  // used while an ISR is executing
  void dispatchPending();
  void runIsr(Irq irq);
  void raise(Irq irq);
  void timerTick(Timer& t);
  void adcTrigger();
  void gpioWrite(GPIO_TypeDef* port, uint16_t pins, bool set);
  Timer* timerFor(const TIM_HandleTypeDef* h);

  Config config_;
  ClockTree clocks_;
  Time now_ = 0;
  bool booted_ = false;
  bool inIsr_ = false;
  bool fatal_ = false;

  uint32_t uwTick_ = 0;
  bool sysTickRunning_ = false;
  Time nextSysTick_ = 0;
  uint32_t lostSysTicks_ = 0;

  bool pending_[kIrqCount] = {};
  bool nvicEnabled_[kIrqCount] = {};

  Timer tim2_, tim3_;
  Uart uart_;
  Adc adc_;
  uint32_t adcClock_ = 0;
  uint32_t uartOverruns_ = 0;

  bool exti0RisingFalling_ = false;
  std::deque<std::pair<Time, bool>> buttonQueue_;

  std::vector<TxByte> tx_;
  std::vector<LedEdge> ledEdges_;
  std::vector<AdcConversion> adcConversions_;
  std::vector<DmaSnapshot> tim2Snapshots_;

 private:
  Board() = default;
};

inline Board& board() { return Board::instance(); }

}  // namespace emu
