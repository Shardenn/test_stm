#include "emulator.hpp"

#include "stm32f411e_discovery.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <thread>

extern "C" {
// Register blocks / calibration words referenced by the fake HAL header.
TIM_TypeDef emu_TIM2, emu_TIM3;
USART_TypeDef emu_USART2;
GPIO_TypeDef emu_GPIOA, emu_GPIOB, emu_GPIOC, emu_GPIOD, emu_GPIOE, emu_GPIOH;
ADC_TypeDef emu_ADC1;
CRC_TypeDef emu_CRC;
I2C_TypeDef emu_I2C1;
SPI_TypeDef emu_SPI1, emu_SPI3;
uint16_t emu_vrefint_cal, emu_ts_cal1, emu_ts_cal2;

// The firmware's main(), renamed with -Dmain=fw_main when main.c is built.
int fw_main(void);
}

namespace emu {
namespace {

constexpr Time kInfinity = std::numeric_limits<Time>::max();
constexpr uint32_t kHseHz = 8000000;  // X2 crystal on the STM32F411E-DISCO
constexpr uint32_t kTimUif = 0x1;
constexpr uint32_t kTimUie = 0x1;

// Boot hand-shake between the firmware thread and the test thread.
enum : int { kStartedTim2 = 1, kStartedTim3 = 2, kStartedAdc = 4, kStartedUartRx = 8, kAllStarted = 15 };
std::atomic<int> g_started{0};
std::atomic<bool> g_fatal{false};
std::atomic<bool> g_booted{false};

[[noreturn]] void die(const char* what) {
  std::fprintf(stderr, "EMULATOR ERROR: %s\n", what);
  std::fflush(stderr);
  std::_Exit(3);
}

uint32_t apbDivider(uint32_t cfg) {
  switch (cfg) {
    case RCC_HCLK_DIV1: return 1;
    case RCC_HCLK_DIV2: return 2;
    case RCC_HCLK_DIV4: return 4;
    case RCC_HCLK_DIV8: return 8;
    case RCC_HCLK_DIV16: return 16;
    default: die("unsupported APB divider");
  }
}

uint32_t adcSampleCycles(uint32_t smp) {
  static const uint32_t table[8] = {3, 15, 28, 56, 84, 112, 144, 480};
  if (smp > 7) die("bad ADC sampling time");
  return table[smp];
}

std::string escape(const std::string& s) {
  std::string out;
  for (unsigned char c : s) {
    if (c >= 0x20 && c < 0x7F && c != '\\') {
      out += static_cast<char>(c);
    } else {
      char buf[8];
      std::snprintf(buf, sizeof buf, "\\x%02X", c);
      out += buf;
    }
  }
  return out;
}

RCC_OscInitTypeDef g_osc;

}  // namespace

Board& Board::instance() {
  static Board b;
  return b;
}

// --------------------------------------------------------------------------
// Boot
// --------------------------------------------------------------------------
void Board::boot() {
  emu_vrefint_cal = config_.vrefintCal;
  emu_ts_cal1 = config_.tsCal1;
  emu_ts_cal2 = config_.tsCal2;
  tim2_.id = 2;
  tim2_.irq = kTim2;
  tim2_.maxCount = 0xFFFFFFFFu;  // TIM2 is a 32-bit timer
  tim3_.id = 3;
  tim3_.irq = kTim3;
  tim3_.maxCount = 0xFFFFu;

  std::thread([] { fw_main(); }).detach();

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (g_started.load(std::memory_order_acquire) != kAllStarted) {
    if (g_fatal.load()) die("firmware entered Error_Handler during initialisation");
    if (std::chrono::steady_clock::now() > deadline) die("firmware did not finish initialisation");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  // The last start call has returned control to main(); give the firmware
  // thread a moment to fall into its (empty) main loop.
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  if (g_fatal.load()) die("firmware entered Error_Handler during initialisation");
  g_booted.store(true, std::memory_order_release);
  booted_ = true;
}

// --------------------------------------------------------------------------
// Scheduler
// --------------------------------------------------------------------------
Time Board::nextEventTime() const {
  Time t = kInfinity;
  if (sysTickRunning_) t = std::min(t, nextSysTick_);
  if (tim2_.running) t = std::min(t, tim2_.nextTick);
  if (tim3_.running) t = std::min(t, tim3_.nextTick);
  if (!adc_.pending.empty()) t = std::min(t, adc_.pending.front().first);
  if (!buttonQueue_.empty()) t = std::min(t, buttonQueue_.front().first);
  if (!uart_.rxQueue.empty()) t = std::min(t, uart_.rxQueue.front().first);
  return t;
}

void Board::processNextEvent() {
  const Time t = nextEventTime();
  // Fixed tie-break order keeps the simulation deterministic.
  if (sysTickRunning_ && nextSysTick_ == t) {
    nextSysTick_ += ms(1);
    raise(kSysTick);
  } else if (tim2_.running && tim2_.nextTick == t) {
    timerTick(tim2_);
  } else if (tim3_.running && tim3_.nextTick == t) {
    timerTick(tim3_);
  } else if (!adc_.pending.empty() && adc_.pending.front().first == t) {
    const uint32_t rank = adc_.pending.front().second;
    adc_.pending.pop_front();
    const uint32_t ch = adc_.ranks[rank];
    uint16_t v = 0;
    if (ch == ADC_CHANNEL_TEMPSENSOR) v = config_.tempRaw(t);
    else if (ch == ADC_CHANNEL_VREFINT) v = config_.vrefRaw(t);
    v &= 0x0FFF;
    adc_.dmaBuffer[adc_.dmaIndex] = v;
    adc_.dmaIndex = (adc_.dmaIndex + 1) % adc_.dmaLength;  // circular DMA
    adcConversions_.push_back({t, ch, v});
  } else if (!buttonQueue_.empty() && buttonQueue_.front().first == t) {
    const bool level = buttonQueue_.front().second;
    buttonQueue_.pop_front();
    const bool old = (emu_GPIOA.IDR & GPIO_PIN_0) != 0;
    if (level) emu_GPIOA.IDR |= GPIO_PIN_0;
    else emu_GPIOA.IDR &= ~static_cast<uint32_t>(GPIO_PIN_0);
    if (old != level && exti0RisingFalling_) raise(kExti0);
  } else if (!uart_.rxQueue.empty() && uart_.rxQueue.front().first == t) {
    const uint8_t b = uart_.rxQueue.front().second;
    uart_.rxQueue.pop_front();
    if (uart_.rxne) {
      uart_.ore = true;  // previous byte not read yet: this one is lost
      ++uartOverruns_;
    } else {
      uart_.rdr = b;
      uart_.rxne = true;
    }
    if (uart_.rxArmed) raise(kUsart2);
  }
}

void Board::runUntil(Time t) {
  if (!booted_) die("runUntil() before boot()");
  dispatchPending();
  for (;;) {
    const Time next = nextEventTime();
    if (next > t) break;
    now_ = next;
    processNextEvent();
    dispatchPending();
  }
  if (now_ < t) now_ = t;
}

void Board::advanceHardware(Time dt) {
  const Time target = now_ + dt;
  for (;;) {
    const Time next = nextEventTime();
    if (next > target) break;
    now_ = next;
    processNextEvent();
  }
  now_ = target;
}

void Board::raise(Irq irq) {
  if (pending_[irq] && irq == kSysTick) ++lostSysTicks_;
  pending_[irq] = true;
}

void Board::dispatchPending() {
  for (;;) {
    int irq = -1;
    for (int i = 0; i < kIrqCount; ++i) {
      if (pending_[i] && (i == kSysTick || nvicEnabled_[i])) {
        irq = i;
        break;
      }
    }
    if (irq < 0) return;
    pending_[irq] = false;
    inIsr_ = true;
    runIsr(static_cast<Irq>(irq));
    inIsr_ = false;
  }
}

void Board::runIsr(Irq irq) {
  switch (irq) {
    case kSysTick:
      HAL_IncTick();
      break;
    case kExti0:
      HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0);
      break;
    case kTim2:
    case kTim3: {
      Timer& t = irq == kTim2 ? tim2_ : tim3_;
      TIM_TypeDef* r = t.handle->Instance;
      if ((r->SR & kTimUif) && (r->DIER & kTimUie)) {
        r->SR &= ~kTimUif;
        if (irq == kTim2 && adc_.dmaBuffer) {
          tim2Snapshots_.push_back({now_, std::vector<uint16_t>(adc_.dmaBuffer, adc_.dmaBuffer + adc_.dmaLength)});
        }
        HAL_TIM_PeriodElapsedCallback(t.handle);
      }
      break;
    }
    case kUsart2: {
      // Mirrors HAL_UART_IRQHandler() for an interrupt-driven reception.
      if (!uart_.rxArmed) break;  // RXNEIE/EIE disabled
      auto receive = [this] {    // UART_Receive_IT() with Size == 1
        *uart_.rxPtr = uart_.rdr;
        uart_.rxne = false;
        uart_.rxArmed = false;
        HAL_UART_RxCpltCallback(uart_.handle);
      };
      if (uart_.ore) {
        if (uart_.rxne) receive();
        uart_.ore = false;
        // Overrun is a blocking error: UART_EndRxTransfer() disables the
        // reception (even if the callback re-armed it) and calls the weak
        // HAL_UART_ErrorCallback(), which the firmware does not override.
        uart_.rxArmed = false;
      } else if (uart_.rxne) {
        receive();
      }
      break;
    }
    default:
      break;
  }
}

void Board::timerTick(Timer& t) {
  t.nextTick += t.tickPeriod;
  TIM_TypeDef* r = t.handle->Instance;
  if (r->CNT == r->ARR) {
    r->CNT = 0;
    r->SR |= kTimUif;
    if (r->DIER & kTimUie) raise(t.irq);
    if (t.trgoOnUpdate) adcTrigger();
  } else {
    r->CNT = (r->CNT + 1) & t.maxCount;
  }
}

void Board::adcTrigger() {
  if (!adc_.running || !adc_.triggeredByTim2 || !adc_.pending.empty()) return;
  Time t = now_;
  for (uint32_t rank = 0; rank < adc_.ranks.size(); ++rank) {
    t += adc_.conversionTime;
    adc_.pending.emplace_back(t, rank);
  }
}

// --------------------------------------------------------------------------
// Stimuli / observations
// --------------------------------------------------------------------------
void Board::scheduleButton(Time t, bool pressed) {
  if (!buttonQueue_.empty() && buttonQueue_.back().first > t) die("button events must be scheduled in order");
  buttonQueue_.emplace_back(t, pressed);
}

void Board::uartSendAt(Time t, const std::string& bytes) {
  if (uart_.frameTime == 0) die("UART not initialised");
  Time start = t;
  if (!uart_.rxQueue.empty()) start = std::max(start, uart_.rxQueue.back().first);
  for (unsigned char c : bytes) {
    start += uart_.frameTime;
    uart_.rxQueue.emplace_back(start, c);
  }
}

std::string Board::txText(Time from) const {
  std::string s;
  for (const TxByte& b : tx_) {
    if (b.end >= from) s += b.c;
  }
  return s;
}

std::vector<TxLine> Board::txLines(Time from) const {
  std::vector<TxLine> lines;
  std::string cur;
  Time start = 0;
  for (const TxByte& b : tx_) {
    if (cur.empty()) start = b.end - uart_.frameTime;
    cur += b.c;
    if (cur.size() >= 2 && cur.compare(cur.size() - 2, 2, "\r\n") == 0) {
      if (start >= from) lines.push_back({start, cur.substr(0, cur.size() - 2)});
      cur.clear();
    }
  }
  return lines;
}

bool Board::ledOn(Led led) const {
  return (emu_GPIOD.ODR & (GPIO_PIN_12 << static_cast<int>(led))) != 0;
}

std::vector<LedEdge> Board::ledEdges(Led led, Time from) const {
  std::vector<LedEdge> out;
  for (const LedEdge& e : ledEdges_) {
    if (e.led == led && e.t >= from) out.push_back(e);
  }
  return out;
}

const TIM_HandleTypeDef* Board::timerHandle(int tim) const {
  return tim == 2 ? tim2_.handle : tim3_.handle;
}

std::string Board::trace() const {
  static const char* kLedNames[] = {"LED4", "LED3", "LED5", "LED6"};
  std::ostringstream os;
  const std::vector<TxLine> lines = txLines();
  size_t li = 0, ei = 0;
  while (li < lines.size() || ei < ledEdges_.size()) {
    const bool takeLine =
        ei >= ledEdges_.size() || (li < lines.size() && lines[li].start <= ledEdges_[ei].t);
    if (takeLine) {
      os << lines[li].start / 1000 << "ns TX \"" << escape(lines[li].text) << "\"\n";
      ++li;
    } else {
      const LedEdge& e = ledEdges_[ei++];
      os << e.t / 1000 << "ns " << kLedNames[static_cast<int>(e.led)] << (e.on ? " ON" : " OFF") << "\n";
    }
  }
  // Unterminated output, if any.
  size_t terminated = 0;
  for (const TxLine& l : lines) terminated += l.text.size() + 2;
  if (terminated < tx_.size()) {
    std::string rest;
    for (size_t i = terminated; i < tx_.size(); ++i) rest += tx_[i].c;
    os << "partial TX \"" << escape(rest) << "\"\n";
  }
  os << "end " << now_ / 1000 << "ns tick=" << uwTick_ << " lostTicks=" << lostSysTicks_
     << " overruns=" << uartOverruns_ << " rxArmed=" << uart_.rxArmed
     << " tim2Isr=" << tim2Snapshots_.size() << " fatal=" << fatal_ << "\n";
  return os.str();
}

void Board::gpioWrite(GPIO_TypeDef* port, uint16_t pins, bool set) {
  const uint32_t before = port->ODR;
  if (set) port->ODR |= pins;
  else port->ODR &= ~static_cast<uint32_t>(pins);
  if (port == GPIOD) {
    for (int i = 0; i < 4; ++i) {
      const uint32_t mask = GPIO_PIN_12 << i;
      if ((before ^ port->ODR) & mask) ledEdges_.push_back({now_, static_cast<Led>(i), (port->ODR & mask) != 0});
    }
  }
}

Board::Timer* Board::timerFor(const TIM_HandleTypeDef* h) {
  if (h->Instance == TIM2) return &tim2_;
  if (h->Instance == TIM3) return &tim3_;
  die("unsupported timer instance");
}

// --------------------------------------------------------------------------
// HAL back-end
// --------------------------------------------------------------------------
namespace {

Board::Irq irqFor(IRQn_Type n, bool* ok) {
    *ok = true;
    switch (n) {
      case EXTI0_IRQn: return Board::kExti0;
      case TIM2_IRQn: return Board::kTim2;
      case TIM3_IRQn: return Board::kTim3;
      case USART2_IRQn: return Board::kUsart2;
      default: *ok = false; return Board::kSysTick;
    }
}

void clockConfig(const RCC_ClkInitTypeDef* clk) {
    ClockTree& c = Board::instance().clocks_;
    if (clk->SYSCLKSource != RCC_SYSCLKSOURCE_PLLCLK) die("only PLL as SYSCLK is modelled");
    if (g_osc.PLL.PLLState != RCC_PLL_ON || g_osc.PLL.PLLSource != RCC_PLLSOURCE_HSE) die("PLL must run from HSE");
    const uint64_t vco = static_cast<uint64_t>(kHseHz) / g_osc.PLL.PLLM * g_osc.PLL.PLLN;
    c.sysclk = static_cast<uint32_t>(vco / g_osc.PLL.PLLP);
    if (clk->AHBCLKDivider != RCC_SYSCLK_DIV1) die("only AHB prescaler 1 is modelled");
    c.hclk = c.sysclk;
    const uint32_t apb1 = apbDivider(clk->APB1CLKDivider);
    c.pclk1 = c.hclk / apb1;
    c.pclk2 = c.hclk / apbDivider(clk->APB2CLKDivider);
    c.apb1TimerClk = apb1 == 1 ? c.pclk1 : 2 * c.pclk1;
}

}  // namespace

}  // namespace emu

using emu::Board;
using emu::board;

// ============================================================================
// C entry points used by the firmware
// ============================================================================
extern "C" {

void emu_disable_irq(void) {
  board().fatal_ = true;
  emu::g_fatal.store(true);
  if (emu::g_booted.load()) emu::die("firmware entered Error_Handler (interrupts disabled)");
}

void emu_nop(void) { std::this_thread::yield(); }

HAL_StatusTypeDef HAL_Init(void) {
  Board& b = board();
  b.now_ = b.config_.bootTime;
  b.uwTick_ = static_cast<uint32_t>(b.now_ / emu::ms(1));
  b.nextSysTick_ = emu::ms(b.uwTick_ + 1);
  b.sysTickRunning_ = true;
  return HAL_OK;
}

uint32_t HAL_GetTick(void) { return board().uwTick_; }
void HAL_IncTick(void) { ++board().uwTick_; }

void HAL_NVIC_SetPriority(IRQn_Type, uint32_t, uint32_t) {}

void HAL_NVIC_EnableIRQ(IRQn_Type IRQn) {
  bool ok = false;
  const Board::Irq irq = emu::irqFor(IRQn, &ok);
  if (ok) board().nvicEnabled_[irq] = true;
}

HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef* osc) {
  emu::g_osc = *osc;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_RCC_ClockConfig(RCC_ClkInitTypeDef* clk, uint32_t) {
  emu::clockConfig(clk);
  return HAL_OK;
}

// ---- GPIO -----------------------------------------------------------------
void HAL_GPIO_Init(GPIO_TypeDef* port, GPIO_InitTypeDef* init) {
  if (port == GPIOA && (init->Pin & GPIO_PIN_0)) {
    board().exti0RisingFalling_ = init->Mode == GPIO_MODE_IT_RISING_FALLING;
  }
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef* port, uint16_t pin) {
  const uint32_t reg = port == GPIOA ? port->IDR : port->ODR;
  return (reg & pin) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

void HAL_GPIO_WritePin(GPIO_TypeDef* port, uint16_t pin, GPIO_PinState state) {
  board().gpioWrite(port, pin, state == GPIO_PIN_SET);
}

void HAL_GPIO_TogglePin(GPIO_TypeDef* port, uint16_t pin) {
  Board& b = board();
  const uint32_t set = port->ODR & pin;
  b.gpioWrite(port, static_cast<uint16_t>(pin & ~set), true);
  b.gpioWrite(port, static_cast<uint16_t>(set), false);
}

void HAL_GPIO_EXTI_IRQHandler(uint16_t pin) { HAL_GPIO_EXTI_Callback(pin); }

// ---- Peripherals that are configured but not modelled ----------------------
HAL_StatusTypeDef HAL_CRC_Init(CRC_HandleTypeDef*) { return HAL_OK; }
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef*) { return HAL_OK; }
HAL_StatusTypeDef HAL_I2S_Init(I2S_HandleTypeDef*) { return HAL_OK; }
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef*) { return HAL_OK; }
void MX_PDM2PCM_Init(void) {}

// ---- ADC ------------------------------------------------------------------
HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef* h) {
  Board& b = board();
  auto& adc = b.adc_;
  adc.handle = h;
  adc.ranks.assign(h->Init.NbrOfConversion, 0xFFFFFFFFu);
  static const uint32_t presc[4] = {2, 4, 6, 8};
  const uint32_t adcclk = b.clocks_.pclk2 / presc[(h->Init.ClockPrescaler >> 16) & 3];
  if (h->Init.Resolution != ADC_RESOLUTION_12B) emu::die("only 12-bit ADC resolution is modelled");
  adc.triggeredByTim2 = h->Init.ExternalTrigConv == ADC_EXTERNALTRIGCONV_T2_TRGO &&
                        h->Init.ExternalTrigConvEdge == ADC_EXTERNALTRIGCONVEDGE_RISING &&
                        h->Init.ContinuousConvMode == DISABLE;
  // Conversion time is set by HAL_ADC_ConfigChannel (sampling time + 12 cycles).
  adc.conversionTime = 0;
  b.adcClock_ = adcclk;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef*, ADC_ChannelConfTypeDef* cfg) {
  Board& b = board();
  auto& adc = b.adc_;
  if (cfg->Rank < 1 || cfg->Rank > adc.ranks.size()) emu::die("bad ADC rank");
  adc.ranks[cfg->Rank - 1] = cfg->Channel;
  const uint64_t cycles = emu::adcSampleCycles(cfg->SamplingTime) + 12;
  const emu::Time t = cycles * 1000000000000ULL / b.adcClock_;
  if (adc.conversionTime != 0 && adc.conversionTime != t) emu::die("per-channel sampling times must match");
  adc.conversionTime = t;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef*, uint32_t* data, uint32_t length) {
  Board& b = board();
  auto& adc = b.adc_;
  // The DMA stream is configured half-word/half-word, circular (see HAL_ADC_MspInit).
  adc.dmaBuffer = reinterpret_cast<uint16_t*>(data);
  adc.dmaLength = length;
  adc.dmaIndex = 0;
  adc.running = true;
  emu::g_started.fetch_or(emu::kStartedAdc, std::memory_order_release);
  return HAL_OK;
}

// ---- TIM ------------------------------------------------------------------
HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef* h) {
  Board& b = board();
  Board::Timer* t = b.timerFor(h);
  t->handle = h;
  TIM_TypeDef* r = h->Instance;
  r->PSC = h->Init.Prescaler;
  r->ARR = h->Init.Period;
  r->CNT = 0;
  r->SR = 0;  // TIM_Base_SetConfig clears the UIF raised by the UG event
  if (h->Init.CounterMode != TIM_COUNTERMODE_UP) emu::die("only up-counting timers are modelled");
  if (h->Init.AutoReloadPreload != TIM_AUTORELOAD_PRELOAD_DISABLE) emu::die("ARR preload is not modelled");
  t->tickPeriod = (static_cast<uint64_t>(r->PSC) + 1) * 1000000000000ULL / b.clocks_.apb1TimerClk;
  // HAL_TIM_Base_MspInit() enables the timer IRQ in the NVIC.
  b.nvicEnabled_[t->irq] = true;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_ConfigClockSource(TIM_HandleTypeDef*, TIM_ClockConfigTypeDef* cfg) {
  if (cfg->ClockSource != TIM_CLOCKSOURCE_INTERNAL) emu::die("only the internal timer clock is modelled");
  return HAL_OK;
}

HAL_StatusTypeDef HAL_TIMEx_MasterConfigSynchronization(TIM_HandleTypeDef* h, TIM_MasterConfigTypeDef* cfg) {
  board().timerFor(h)->trgoOnUpdate = cfg->MasterOutputTrigger == TIM_TRGO_UPDATE;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef* h) {
  Board& b = board();
  Board::Timer* t = b.timerFor(h);
  h->Instance->DIER |= emu::kTimUie;
  t->running = true;
  t->nextTick = b.now_ + t->tickPeriod;
  emu::g_started.fetch_or(t->id == 2 ? emu::kStartedTim2 : emu::kStartedTim3, std::memory_order_release);
  return HAL_OK;
}

// ---- UART -----------------------------------------------------------------
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef* h) {
  Board& b = board();
  if (h->Instance != USART2) emu::die("only USART2 is modelled");
  b.uart_.handle = h;
  // BRR exactly as UART_SetConfig() computes it (oversampling by 16).
  const uint64_t pclk = b.clocks_.pclk1;
  const uint64_t baud = h->Init.BaudRate;
  const uint64_t div = pclk * 25 / (4 * baud);
  const uint64_t mant = div / 100;
  const uint64_t frac = ((div - mant * 100) * 16 + 50) / 100;
  const uint64_t brr = (mant << 4) + (frac & 0xF0) + (frac & 0x0F);
  h->Instance->BRR = static_cast<uint32_t>(brr);
  const uint64_t bits = 1 + (h->Init.WordLength == UART_WORDLENGTH_9B ? 9 : 8) +
                        (h->Init.StopBits == UART_STOPBITS_2 ? 2 : 1);
  b.uart_.frameTime = bits * brr * 1000000000000ULL / pclk;
  // HAL_UART_MspInit() enables USART2_IRQn.
  b.nvicEnabled_[Board::kUsart2] = true;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef* h, const uint8_t* data, uint16_t size, uint32_t) {
  Board& b = board();
  if (h != b.uart_.handle) emu::die("transmit on unknown UART");
  for (uint16_t i = 0; i < size; ++i) {
    b.advanceHardware(b.uart_.frameTime);  // busy-waits on TXE/TC
    b.tx_.push_back({b.now_, static_cast<char>(data[i])});
  }
  return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef* h, uint8_t* data, uint16_t size) {
  Board& b = board();
  if (h != b.uart_.handle) emu::die("receive on unknown UART");
  if (size != 1) emu::die("only single-byte interrupt reception is modelled");
  if (b.uart_.rxArmed) return HAL_BUSY;
  b.uart_.rxPtr = data;
  b.uart_.rxArmed = true;
  if (b.uart_.rxne || b.uart_.ore) b.raise(Board::kUsart2);
  emu::g_started.fetch_or(emu::kStartedUartRx, std::memory_order_release);
  return HAL_OK;
}

// ---- BSP (same behaviour as Drivers/BSP/STM32F411E-Discovery) -------------
static const uint16_t kLedPins[4] = {GPIO_PIN_12, GPIO_PIN_13, GPIO_PIN_14, GPIO_PIN_15};

void BSP_LED_Init(Led_TypeDef led) { HAL_GPIO_WritePin(GPIOD, kLedPins[led], GPIO_PIN_RESET); }
void BSP_LED_On(Led_TypeDef led) { HAL_GPIO_WritePin(GPIOD, kLedPins[led], GPIO_PIN_SET); }
void BSP_LED_Off(Led_TypeDef led) { HAL_GPIO_WritePin(GPIOD, kLedPins[led], GPIO_PIN_RESET); }
void BSP_LED_Toggle(Led_TypeDef led) { HAL_GPIO_TogglePin(GPIOD, kLedPins[led]); }
void BSP_PB_Init(Button_TypeDef, ButtonMode_TypeDef) {}
uint32_t BSP_PB_GetState(Button_TypeDef) { return HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0); }
uint32_t BSP_GetVersion(void) { return 0; }

}  // extern "C"
