// Unit tests of the C++ application classes against the fake board.
#include <climits>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "app/application.hpp"
#include "app/button_handler.hpp"
#include "app/command_parser.hpp"
#include "app/led_blinker.hpp"
#include "app/serial.hpp"
#include "app/telemetry.hpp"
#include "fake_board.hpp"
#include "test_framework.hpp"

using namespace app;
using Lines = std::vector<std::string>;

namespace {

std::string str(const Line& l) { return std::string(l.data(), l.size()); }

std::string joined(const Lines& v) {
  std::string s;
  for (const std::string& l : v) s += "  | " + l + "\n";
  return s;
}

Command feedAll(CommandParser& p, const std::string& s) {
  Command last = {Command::Type::None, 0};
  for (char c : s) {
    const Command c2 = p.feed(c);
    if (c2.type != Command::Type::None) last = c2;
  }
  return last;
}

const char* name(Command::Type t) {
  switch (t) {
    case Command::Type::None: return "None";
    case Command::Type::Query: return "Query";
    case Command::Type::SetOnPeriod: return "SetOn";
    case Command::Type::SetOffPeriod: return "SetOff";
    case Command::Type::Invalid: return "Invalid";
  }
  return "?";
}

}  // namespace

// ---------------------------------------------------------------- Line

TEST_CASE(line_formats_text_and_numbers, "unit") {
  CHECK_EQ(str(Line("A ") << uint32_t{0}), std::string("A 0"));
  CHECK_EQ(str(Line() << uint32_t{4294967295u}), std::string("4294967295"));
  CHECK_EQ(str(Line() << int32_t{-5}), std::string("-5"));
  CHECK_EQ(str(Line() << int32_t{INT32_MIN}), std::string("-2147483648"));
  CHECK_EQ(str(Line("LED ") << uint32_t{3500} << "L OK"), std::string("LED 3500L OK"));
  CHECK_EQ(str(Line() << 'x' << int32_t{12}), std::string("x12"));
}

TEST_CASE(line_truncates_at_capacity, "unit") {
  Line l;
  for (int i = 0; i < 50; ++i) l << 'a';
  CHECK_EQ(l.size(), static_cast<std::size_t>(Line::kCapacity));
}

TEST_CASE(send_line_appends_crlf_in_one_write, "unit") {
  sendLine(Line("VCC ") << uint32_t{3300});
  sendLine("ERROR");
  CHECK_EQ(fake::board().serial, std::string("VCC 3300\r\nERROR\r\n"));
}

// ---------------------------------------------------------------- CommandParser

TEST_CASE(parser_recognises_commands, "unit 5.A-D") {
  struct Case { std::string in; Command::Type type; uint32_t value; };
  const Case cases[] = {
      {"?\r", Command::Type::Query, 0},          {"?\n", Command::Type::Query, 0},
      {"500L\r", Command::Type::SetOnPeriod, 500}, {"9999L\n", Command::Type::SetOnPeriod, 9999},
      {"3500D\r", Command::Type::SetOffPeriod, 3500}, {"0500D\r", Command::Type::SetOffPeriod, 500},
      {"00999L\r", Command::Type::SetOnPeriod, 999},
      // Kept from the original: '?' followed by NUL is a query.
      {std::string("?\0\r", 3), Command::Type::Query, 0},
  };
  for (const Case& c : cases) {
    CommandParser p;
    const Command cmd = feedAll(p, c.in);
    CHECK_MSG(cmd.type == c.type && cmd.value == c.value, "input '" << c.in << "' -> " << name(cmd.type) << " " << cmd.value);
  }
}

TEST_CASE(parser_rejects_invalid_lines, "unit 5.D 5.E") {
  const char* inputs[] = {"499L\r", "10000L\r", "0L\r", "L\r",   "D\r",     "abc\r",   "?x\r",  "??\r",
                          "x?\r",   "12a4L\r",  "-500L\r", "+500L\r", " 500L\r", "3500l\r", "5000X\r", "LD\r",
                          "1234567L\r"};
  for (const char* in : inputs) {
    CommandParser p;
    const Command cmd = feedAll(p, in);
    CHECK_MSG(cmd.type == Command::Type::Invalid, "input '" << in << "' -> " << name(cmd.type));
  }
}

TEST_CASE(parser_returns_none_until_line_end_and_ignores_empty_lines, "unit") {
  CommandParser p;
  for (char c : std::string("3500")) CHECK(p.feed(c).type == Command::Type::None);
  CHECK(p.feed('L').type == Command::Type::None);
  CHECK(p.feed('\r').type == Command::Type::SetOnPeriod);
  CHECK(p.feed('\n').type == Command::Type::None);
  CHECK(p.feed('\r').type == Command::Type::None);
}

TEST_CASE(parser_discards_buffer_on_overflow, "unit") {
  CommandParser p;
  // 6 characters fill the buffer; the 7th is dropped together with them.
  CHECK(feedAll(p, "123456L\r").type == Command::Type::None);
  CHECK(feedAll(p, "?\r").type == Command::Type::Query);
  CHECK(feedAll(p, "99999L\r").type == Command::Type::Invalid);
}

// ---------------------------------------------------------------- Telemetry

TEST_CASE(telemetry_converts_with_calibration, "unit 4.C") {
  const board::AdcCalibration cal = {3300, 1520, 30, 940, 110, 1210};
  Measurement m = Telemetry::convert(940, 1520, cal);
  CHECK_EQ(m.temperatureC, 30);
  CHECK_EQ(m.vccMv, 3300u);
  m = Telemetry::convert(1210, 1700, cal);
  CHECK_EQ(m.temperatureC, 110);
  CHECK_EQ(m.vccMv, 2950u);
  m = Telemetry::convert(900, 1400, cal);  // below the first calibration point
  CHECK_EQ(m.temperatureC, 19);
  CHECK_EQ(m.vccMv, 3582u);
  m = Telemetry::convert(0, 0, cal);  // empty window
  CHECK_EQ(m.vccMv, 0u);
  CHECK_EQ(m.temperatureC, -248);
}

TEST_CASE(telemetry_averages_the_last_10_samples, "unit 4.B") {
  const board::AdcCalibration cal = {3300, 1520, 30, 940, 110, 1210};
  Telemetry t;
  for (int i = 0; i < 5; ++i) t.addSample(1210, 1520);
  // Half the window is still zero.
  CHECK_EQ(t.measure(cal).vccMv, 3300u * 1520u / 760u);
  for (int i = 0; i < 5; ++i) t.addSample(1210, 1520);
  CHECK_EQ(t.measure(cal).temperatureC, 110);
  CHECK_EQ(t.measure(cal).vccMv, 3300u);
  // The 11th sample replaces the oldest one: 9 x 1520 + 1 x 1720 -> 1540.
  t.addSample(1210, 1720);
  CHECK_EQ(t.measure(cal).vccMv, 3300u * 1520u / 1540u);
  // 9 more samples: the window is 1 x 1210 + 9 x 940 -> 967 -> 38 C.
  for (int i = 0; i < 9; ++i) t.addSample(940, 1720);
  CHECK_EQ(t.measure(cal).vccMv, 3300u * 1520u / 1720u);
  CHECK_EQ(t.measure(cal).temperatureC, 38);
  t.addSample(940, 1720);
  CHECK_EQ(t.measure(cal).temperatureC, 30);
}

// ---------------------------------------------------------------- LedBlinker

TEST_CASE(led_toggle_reports_and_reloads_timer, "unit 1 3.A") {
  LedBlinker led;
  CHECK(led.state() == LedState::Off);
  fake::board().tick = 4000;
  led.toggle();
  CHECK_EQ(joined(fake::takeLines()), joined({"LED OFF TIME 4000", "LED ON"}));
  CHECK(led.state() == LedState::On);
  fake::board().tick = 8001;
  led.toggle();
  CHECK_EQ(joined(fake::takeLines()), joined({"LED ON TIME 4001", "LED OFF"}));
  CHECK_EQ(fake::board().ledToggles, 2);
  REQUIRE(fake::board().timerReloads.size() == 2);
  CHECK_EQ(fake::board().timerReloads[0], 3999u);
  CHECK_EQ(fake::board().timerReloads[1], 5999u);
}

TEST_CASE(led_new_period_is_used_on_next_entry, "unit 5.B 5.C") {
  LedBlinker led;
  led.toggle();  // -> ON with 3999
  led.setPeriod(LedState::On, 1234);
  led.setPeriod(LedState::Off, 777);
  led.toggle();  // -> OFF with 777
  led.toggle();  // -> ON with 1234
  const auto& r = fake::board().timerReloads;
  REQUIRE(r.size() == 3);
  CHECK_EQ(r[0], 3999u);
  CHECK_EQ(r[1], 777u);
  CHECK_EQ(r[2], 1234u);
  CHECK_EQ(led.period(LedState::On), 1234u);
}

// ---------------------------------------------------------------- ButtonHandler

namespace {

struct ButtonFixture {
  LedBlinker led;
  Telemetry telemetry;
  ButtonHandler button{led, telemetry};

  void edge(uint32_t tick, bool pressed) {
    fake::board().tick = tick;
    fake::board().button = pressed;
    button.onEdge();
  }
};

}  // namespace

TEST_CASE(button_toggles_on_press_press_release_release, "unit 2.C") {
  ButtonFixture f;
  uint32_t t = 1000;
  const int expectedAfterPress[] = {1, 2, 2, 3, 5, 6, 6, 7};
  const int expectedAfterRelease[] = {1, 2, 3, 4, 5, 6, 7, 8};
  for (int i = 0; i < 8; ++i) {
    f.edge(t, true);
    CHECK_MSG(fake::board().ledToggles == expectedAfterPress[i], "press " << i);
    f.edge(t + 100, false);
    CHECK_MSG(fake::board().ledToggles == expectedAfterRelease[i], "release " << i);
    t += 500;
  }
}

TEST_CASE(button_press_within_20ms_of_release_is_ignored, "unit 2.A") {
  ButtonFixture f;
  f.edge(1000, true);
  f.edge(1100, false);
  fake::takeLines();
  f.edge(1119, true);  // bounce
  CHECK(fake::takeLines().empty());
  f.edge(1119, false);  // release without a handled press
  CHECK(fake::takeLines().empty());
  f.edge(1120, true);
  CHECK_EQ(joined(fake::takeLines()), joined({"LED ON TIME 120", "LED OFF", "BUTTON ON"}));
  f.edge(1121, true);  // repeated press edge
  CHECK(fake::takeLines().empty());
}

TEST_CASE(button_long_press_reports_temperature_and_vcc, "unit 4.D") {
  ButtonFixture f;
  for (int i = 0; i < 10; ++i) f.telemetry.addSample(1100, 1400);
  f.edge(1000, true);
  fake::takeLines();
  f.edge(1500, false);  // exactly 500 ms: no report
  CHECK_EQ(joined(fake::takeLines()), joined({"BUTTON OFF", "BUTTON TIME 500"}));
  f.edge(2000, true);
  fake::takeLines();
  f.edge(2501, false);
  CHECK_EQ(joined(fake::takeLines()), joined({"TEMP 77", "VCC 3582", "BUTTON OFF", "BUTTON TIME 501"}));
}

// ---------------------------------------------------------------- Application

namespace {

void send(Application& a, const std::string& s) {
  for (char c : s) a.onSerialByte(static_cast<uint8_t>(c));
}

}  // namespace

TEST_CASE(application_answers_serial_commands, "unit 5") {
  Application a;
  for (int i = 0; i < 10; ++i) a.onSamplingTimer(1061, 1500);
  send(a, "?\r");
  CHECK_EQ(joined(fake::takeLines()), joined({"VCC 3344", "TEMP 65", "LED 3999L", "LED 5999D"}));
  send(a, "3500L\r\n5500D\rfoo\n");
  CHECK_EQ(joined(fake::takeLines()), joined({"LED 3500L OK", "LED 5500D OK", "ERROR"}));
  send(a, "?\n");
  CHECK_EQ(joined(fake::takeLines()), joined({"VCC 3344", "TEMP 65", "LED 3500L", "LED 5500D"}));
}

TEST_CASE(application_routes_timer_and_button_events, "unit") {
  Application a;
  fake::board().tick = 4000;
  a.onLedTimer();
  CHECK_EQ(joined(fake::takeLines()), joined({"LED OFF TIME 4000", "LED ON"}));
  fake::board().tick = 4100;
  fake::board().button = true;
  a.onButtonEdge();
  CHECK_EQ(joined(fake::takeLines()), joined({"LED ON TIME 100", "LED OFF", "BUTTON ON"}));
}

// ---------------------------------------------------------------- main.c

namespace {

std::string readFile(const char* path) {
  std::ifstream f(path, std::ios::binary);
  std::ostringstream os;
  os << f.rdbuf();
  std::string s = os.str();
  std::string out;  // normalise line endings
  for (char c : s) {
    if (c != '\r') out += c;
  }
  return out;
}

// Replaces the content of every USER CODE section with its name.
std::string withoutUserCode(const std::string& s, int* sections) {
  std::string out;
  size_t pos = 0;
  *sections = 0;
  for (;;) {
    const size_t b = s.find("/* USER CODE BEGIN ", pos);
    if (b == std::string::npos) break;
    const size_t nameEnd = s.find("*/", b);
    const std::string name = s.substr(b + 19, nameEnd - b - 19);
    const size_t e = s.find("/* USER CODE END " + name + "*/", nameEnd);
    if (e == std::string::npos) return "unterminated USER CODE " + name;
    out += s.substr(pos, b - pos) + "<" + name + ">";
    pos = s.find("*/", e) + 2;
    ++*sections;
  }
  return out + s.substr(pos);
}

}  // namespace

TEST_CASE(main_c_changes_only_inside_user_code_sections, "task") {
  const std::string current = readFile(MAIN_C_PATH);
  const std::string original = readFile(REFERENCE_MAIN_C_PATH);
  REQUIRE(!current.empty() && !original.empty());
  int n1 = 0, n2 = 0;
  const std::string a = withoutUserCode(current, &n1);
  const std::string b = withoutUserCode(original, &n2);
  CHECK_EQ(n1, n2);
  CHECK(a == b);
  CHECK(current != original);
}
