// Requirement 5: serial commands '?', "XXXXL", "XXXXD" and error handling.
#include "helpers.hpp"

using namespace th;

namespace {

using Lines = std::vector<std::string>;

Lines one(const char* s) { return Lines{s}; }

}  // namespace

TEST_CASE(query_reports_vcc_temp_and_led_durations, "5.A") {
  boot();
  b().runUntil(ms(500));
  const emu::Config& cfg = b().config();
  const Lines expected = {
      "VCC " + std::to_string(expectedVcc(1700, cfg.vrefintCal)),
      "TEMP " + std::to_string(expectedTemp(1060, cfg.tsCal1, cfg.tsCal2)),
      // Characterisation: the firmware stores and reports the TIM3 reload
      // value (duration - 1 ms), the task's example shows 4000L / 6000D.
      "LED 3999L",
      "LED 5999D",
  };
  CHECK_EQ(joined(command("?\r")), joined(expected));
}

TEST_CASE(set_on_duration_applies_from_next_on_phase, "5.B 5.D") {
  boot();
  b().runUntil(ms(1000));  // LED off, switches on at ~4 s
  CHECK_EQ(joined(command("3500L\r")), joined(one("LED 3500L OK")));
  b().runUntil(ms(5000));  // now on (since ~4 s) with the 3500 ms setting
  // Changing it while ON must not affect the running phase.
  CHECK_EQ(joined(command("2000L\r")), joined(one("LED 2000L OK")));
  b().runUntil(ms(20000));
  const auto e = led4Edges();
  REQUIRE(e.size() >= 4);
  CHECK(e[0].on);
  CHECK_BETWEEN(e[1].t - e[0].t, ms(3496), ms(3506));  // reload 3500 -> 3501 ms
  CHECK_BETWEEN(e[2].t - e[1].t, ms(5995), ms(6005));  // off time unchanged
  CHECK_BETWEEN(e[3].t - e[2].t, ms(1996), ms(2006));  // reload 2000 -> 2001 ms
  const Lines q = command("?\r");
  REQUIRE(q.size() == 4);
  CHECK_EQ(q[2], std::string("LED 2000L"));
  CHECK_EQ(q[3], std::string("LED 5999D"));
}

TEST_CASE(set_off_duration_applies_from_next_off_phase, "5.C 5.D") {
  boot();
  b().runUntil(ms(5000));  // on since ~4 s, off at ~8 s
  CHECK_EQ(joined(command("5500D\r")), joined(one("LED 5500D OK")));
  b().runUntil(ms(9000));  // off phase with the 5500 ms setting is running
  CHECK_EQ(joined(command("700D\r")), joined(one("LED 700D OK")));
  b().runUntil(ms(25000));
  const auto e = led4Edges();
  REQUIRE(e.size() >= 5);
  CHECK(!e[1].on);
  CHECK_BETWEEN(e[2].t - e[1].t, ms(5496), ms(5506));
  CHECK_BETWEEN(e[3].t - e[2].t, ms(3995), ms(4005));
  CHECK_BETWEEN(e[4].t - e[3].t, ms(696), ms(706));
  const Lines q = command("?\r");
  REQUIRE(q.size() == 4);
  CHECK_EQ(q[2], std::string("LED 3999L"));
  CHECK_EQ(q[3], std::string("LED 700D"));
}

TEST_CASE(duration_limits_500_to_9999, "5.D 5.E") {
  boot();
  b().runUntil(ms(200));
  struct Case { const char* in; const char* out; };
  const Case cases[] = {
      {"499L\r", "ERROR"},   {"500L\r", "LED 500L OK"},   {"9999L\r", "LED 9999L OK"},
      {"10000L\r", "ERROR"}, {"0L\r", "ERROR"},           {"1L\r", "ERROR"},
      {"499D\r", "ERROR"},   {"500D\r", "LED 500D OK"},   {"9999D\r", "LED 9999D OK"},
      {"10000D\r", "ERROR"}, {"0D\r", "ERROR"},
  };
  for (const Case& c : cases) CHECK_EQ(joined(command(c.in)), joined(one(c.out)));
  const Lines q = command("?\r");
  REQUIRE(q.size() == 4);
  CHECK_EQ(q[2], std::string("LED 9999L"));
  CHECK_EQ(q[3], std::string("LED 9999D"));
}

TEST_CASE(invalid_input_reports_error, "5.E") {
  boot();
  b().runUntil(ms(200));
  const char* inputs[] = {"abc\r",   "12a4L\r", "L\r",     "D\r",     "?x\r",   "??\r",
                          "x?\r",    "3500X\r", "-500L\r", "+500L\r", " 500L\r", "500 L\r",
                          "3500l\r", "3500d\r", "35.5L\r", "\x01\r",  "LD\r",   "5000LD\r"};
  for (const char* in : inputs) CHECK_EQ(joined(command(in)), joined(one("ERROR")));
  const Lines q = command("?\r");  // nothing was changed
  REQUIRE(q.size() == 4);
  CHECK_EQ(q[2], std::string("LED 3999L"));
  CHECK_EQ(q[3], std::string("LED 5999D"));
}

TEST_CASE(cr_lf_and_crlf_terminate_commands, "5") {
  boot();
  b().runUntil(ms(200));
  CHECK_EQ(command("?\r").size(), 4u);
  CHECK_EQ(command("?\n").size(), 4u);
  CHECK_EQ(command("?\r\n").size(), 4u);  // the empty line after CR is ignored
  CHECK(command("\r\n\r\n").empty());
  CHECK_EQ(joined(command("1234L\n")), joined(one("LED 1234L OK")));
  CHECK_EQ(joined(command("4321D\r\n")), joined(one("LED 4321D OK")));
}

TEST_CASE(reply_echoes_value_without_leading_zeros_characterization, "5.B 5.C") {
  // Characterisation: the reply is built from the parsed number, so leading
  // zeros are not repeated ("0500L" -> "LED 500L OK").
  boot();
  b().runUntil(ms(200));
  CHECK_EQ(joined(command("0500L\r")), joined(one("LED 500L OK")));
  CHECK_EQ(joined(command("00600D\r")), joined(one("LED 600D OK")));
}

TEST_CASE(overlong_input_characterization, "5.E") {
  // Characterisation: the receive buffer holds 6 characters.  The 7th
  // character is dropped and the buffer restarts, so "1234567L" is parsed as
  // "L" (ERROR) and "123456L" leaves an empty buffer (no answer at all).
  boot();
  b().runUntil(ms(200));
  CHECK_EQ(joined(command("1234567L\r")), joined(one("ERROR")));
  CHECK(command("123456L\r").empty());
  CHECK_EQ(joined(command("99999L\r")), joined(one("ERROR")));  // 6 chars, out of range
  CHECK_EQ(command("?\r").size(), 4u);                           // parser recovered
}

TEST_CASE(back_to_back_commands_overrun_stops_reception_characterization, "5") {
  // Characterisation: commands are answered inside the RX interrupt with a
  // blocking transmit.  If two more bytes arrive meanwhile, USART2 overruns,
  // HAL_UART_IRQHandler aborts the reception and nothing re-arms it.
  boot();
  b().runUntil(ms(200));
  const Lines l = command("?\r?\r", ms(50));
  CHECK_EQ(l.size(), 4u);  // only the first query is answered
  CHECK(b().uartOverruns() >= 1);
  CHECK(!b().uartRxArmed());
  CHECK(command("?\r").empty());
}

TEST_CASE(single_byte_during_answer_is_not_lost, "5") {
  boot();
  b().runUntil(ms(200));
  // "?\r\n": the LF arrives while the answer to CR is being sent.
  CHECK_EQ(command("?\r\n", ms(30)).size(), 4u);
  CHECK(b().uartRxArmed());
  CHECK_EQ(b().uartOverruns(), 0u);
  CHECK_EQ(joined(command("777L\r")), joined(one("LED 777L OK")));
}
