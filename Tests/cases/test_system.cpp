// Board bring-up checks: clocks, peripheral configuration and initial state.
#include "helpers.hpp"

using namespace th;

TEST_CASE(boot_configures_clocks_timers_and_leds, "init") {
  boot();
  const emu::ClockTree& c = b().clocks();
  CHECK_EQ(c.sysclk, 96000000u);
  CHECK_EQ(c.pclk1, 24000000u);
  CHECK_EQ(c.pclk2, 96000000u);
  CHECK_EQ(c.apb1TimerClk, 48000000u);

  // TIM3 drives the LED: 1 kHz counter clock, so ARR is in milliseconds.
  CHECK_EQ(b().timerTickPeriod(3), us(1000));
  CHECK_EQ(b().timerHandle(3)->Instance->ARR, 3999u);
  // TIM2 triggers the ADC: 10 kHz counter clock, period 134 counts.
  CHECK_EQ(b().timerTickPeriod(2), us(100));
  CHECK_EQ(b().timerHandle(2)->Instance->ARR, 133u);

  CHECK(!b().fatalErrorEntered());
  CHECK(!b().ledOn(Led::Led3));
  CHECK(!b().ledOn(Led::Led4));
  CHECK(!b().ledOn(Led::Led5));  // red LED = Error_Handler
  CHECK(!b().ledOn(Led::Led6));
  CHECK(b().uartRxArmed());

  b().runFor(ms(100));
  CHECK(b().txBytes().empty());  // nothing is printed before the first LED change
}

TEST_CASE(serial_port_is_115200_8E1, "3.A") {
  boot();
  const UART_HandleTypeDef* u = b().uartHandle();
  REQUIRE(u != nullptr);
  CHECK_EQ(u->Init.BaudRate, 115200u);
  CHECK_EQ(u->Init.WordLength, UART_WORDLENGTH_9B);  // 8 data bits + parity bit
  CHECK_EQ(u->Init.Parity, UART_PARITY_EVEN);
  CHECK_EQ(u->Init.StopBits, UART_STOPBITS_1);
  CHECK_EQ(u->Init.Mode, UART_MODE_TX_RX);
  // 11-bit frame at 24 MHz / 208 = 115385 Bd
  CHECK_BETWEEN(b().uartFrameTime(), us(95), us(96));
}

TEST_CASE(all_output_lines_are_crlf_terminated, "3.A") {
  boot();
  b().runFor(ms(1000));
  press(ms(700));
  b().runFor(ms(100));
  command("?\r");
  command("3500L\r");
  command("xyz\r");
  b().runFor(ms(12000));
  const std::string s = b().txText();
  REQUIRE(!s.empty());
  CHECK(s.size() >= 2 && s.compare(s.size() - 2, 2, "\r\n") == 0);
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\r') CHECK_MSG(i + 1 < s.size() && s[i + 1] == '\n', "at offset " << i);
    if (s[i] == '\n') CHECK_MSG(i > 0 && s[i - 1] == '\r', "at offset " << i);
  }
}
