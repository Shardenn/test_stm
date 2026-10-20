// Requirement 4: 75 Hz sampling of the temperature sensor and VREFINT,
// average of 10 samples, integer conversion, report on long press.
#include "helpers.hpp"

using namespace th;

namespace {

uint16_t g_temp = 1060;
uint16_t g_vref = 1700;

void bootWithVariableInputs() {
  b().config().tempRaw = [](Time) { return g_temp; };
  b().config().vrefRaw = [](Time) { return g_vref; };
  boot();
}

struct Reading {
  bool ok = false;
  long long vcc = 0, temp = 0;
};

Reading parseQuery(const std::vector<std::string>& l) {
  Reading r;
  r.ok = l.size() == 4 && numberAfter(l[0], "VCC ", &r.vcc) && numberAfter(l[1], "TEMP ", &r.temp);
  return r;
}

}  // namespace

TEST_CASE(adc_is_sampled_at_75hz, "4.A") {
  boot();
  b().runFor(ms(10000));
  size_t temp = 0, vref = 0;
  for (const auto& c : b().adcConversions()) {
    if (c.channel == ADC_CHANNEL_TEMPSENSOR) ++temp;
    if (c.channel == ADC_CHANNEL_VREFINT) ++vref;
  }
  const double tempHz = temp / 10.0, vrefHz = vref / 10.0;
  const double isrHz = b().tim2IsrDmaSnapshots().size() / 10.0;
  tf::log("temperature " + std::to_string(tempHz) + " Hz, vrefint " + std::to_string(vrefHz) +
          " Hz, sampling ISR " + std::to_string(isrHz) + " Hz");
  CHECK_BETWEEN(tempHz, 74.0, 76.0);
  CHECK_BETWEEN(vrefHz, 74.0, 76.0);
  CHECK_BETWEEN(isrHz, 74.0, 76.0);
}

TEST_CASE(long_press_reports_temp_and_vcc_once, "4.C 4.D") {
  boot();
  b().runUntil(ms(1000));
  const Time t0 = b().now();
  press(ms(673));
  b().runFor(ms(100));
  const auto l = lines(t0);
  REQUIRE(l.size() == 7);
  CHECK_EQ(l[2], std::string("BUTTON ON"));
  const emu::Config& cfg = b().config();
  CHECK_EQ(l[3], "TEMP " + std::to_string(expectedTemp(1060, cfg.tsCal1, cfg.tsCal2)));
  CHECK_EQ(l[4], "VCC " + std::to_string(expectedVcc(1700, cfg.vrefintCal)));
  CHECK_EQ(l[5], std::string("BUTTON OFF"));
  long long v = 0;
  CHECK_MSG(numberAfter(l[6], "BUTTON TIME ", &v) && v >= 668 && v <= 673, l[6]);
}

TEST_CASE(short_press_does_not_report_adc, "4.D") {
  boot();
  b().runUntil(ms(1000));
  const Time t0 = b().now();
  press(ms(300));
  b().runFor(ms(100));
  for (const std::string& l : lines(t0)) {
    CHECK_MSG(l.compare(0, 5, "TEMP ") != 0 && l.compare(0, 4, "VCC ") != 0, l);
  }
}

TEST_CASE(long_press_threshold_is_more_than_500ms, "4.D") {
  boot();
  b().runUntil(ms(500));
  int reported = 0, notReported = 0;
  for (int d = 494; d <= 510; ++d) {
    const Time t0 = b().now();
    press(ms(d));
    b().runFor(ms(300));
    long long duration = -1;
    bool hasTemp = false, hasVcc = false;
    for (const std::string& l : lines(t0)) {
      numberAfter(l, "BUTTON TIME ", &duration);
      hasTemp |= l.compare(0, 5, "TEMP ") == 0;
      hasVcc |= l.compare(0, 4, "VCC ") == 0;
    }
    REQUIRE(duration >= 0);
    CHECK_MSG(hasTemp == (duration > 500) && hasVcc == (duration > 500), "BUTTON TIME " << duration);
    (duration > 500 ? reported : notReported)++;
  }
  CHECK(reported > 0);
  CHECK(notReported > 0);
}

TEST_CASE(values_are_the_average_of_the_last_10_samples, "4.B") {
  // Every conversion yields a different value.
  b().config().tempRaw = [](Time t) { return static_cast<uint16_t>(950 + (t / us(100) * 7919) % 400); };
  b().config().vrefRaw = [](Time t) { return static_cast<uint16_t>(1450 + (t / us(100) * 104729) % 400); };
  boot();
  const emu::Config& cfg = b().config();
  for (int i = 0; i < 6; ++i) {
    b().runUntil(ms(2000 + 333 * i));
    const Time t0 = b().now();
    const Reading r = parseQuery(command("?\r"));
    REQUIRE(r.ok);
    const Time answered = b().txLines(t0).front().start;
    // The samples the firmware saw: one per TIM2 interrupt before the answer.
    std::vector<const emu::DmaSnapshot*> seen;
    for (const auto& s : b().tim2IsrDmaSnapshots()) {
      if (s.t <= answered) seen.push_back(&s);
    }
    REQUIRE(seen.size() >= 10);
    uint32_t sumTemp = 0, sumVref = 0;
    for (size_t k = seen.size() - 10; k < seen.size(); ++k) {
      sumTemp += seen[k]->data[0];
      sumVref += seen[k]->data[1];
    }
    CHECK_EQ(r.vcc, expectedVcc(sumVref / 10, cfg.vrefintCal));
    CHECK_EQ(r.temp, expectedTemp(sumTemp / 10, cfg.tsCal1, cfg.tsCal2));
  }
}

TEST_CASE(temperature_and_vcc_integer_conversion, "4.C") {
  bootWithVariableInputs();
  const emu::Config& cfg = b().config();
  struct Case { uint16_t temp, vref; long long expTemp, expVcc; };
  const Case cases[] = {
      {940, 1520, 30, 3300},    // exactly at the calibration points
      {1210, 1700, 110, 2950},
      {1100, 1400, 77, 3582},
      {1000, 1900, 47, 2640},
      {1061, 1500, 65, 3344},
  };
  for (const Case& c : cases) {
    g_temp = c.temp;
    g_vref = c.vref;
    b().runFor(ms(300));  // > 10 sampling periods: the window holds only the new value
    const Reading r = parseQuery(command("?\r"));
    REQUIRE(r.ok);
    CHECK_EQ(r.temp, c.expTemp);
    CHECK_EQ(r.vcc, c.expVcc);
    CHECK_EQ(r.temp, expectedTemp(c.temp, cfg.tsCal1, cfg.tsCal2));
    CHECK_EQ(r.vcc, expectedVcc(c.vref, cfg.vrefintCal));
  }
}

TEST_CASE_EX(temperature_below_30C_is_converted_correctly, "4.C", tf::kRewriteOnly,
             "original computes (avg - TS_CAL1) unsigned: wraps below 30 C") {
  bootWithVariableInputs();
  g_temp = 900;  // below TS_CAL1 (940 = 30 C)
  b().runFor(ms(300));
  Reading r = parseQuery(command("?\r"));
  REQUIRE(r.ok);
  CHECK_EQ(r.temp, 19);
  g_temp = 800;
  b().runFor(ms(300));
  r = parseQuery(command("?\r"));
  REQUIRE(r.ok);
  CHECK_EQ(r.temp, -11);
}

TEST_CASE_EX(query_before_first_sample_does_not_crash, "4.C", tf::kRewriteOnly,
             "original divides by an all-zero average (0 on Cortex-M4, crash on a PC)") {
  boot();
  // First sampling interrupt comes ~13 ms after start-up; ask before that.
  const auto l = command("?\r", ms(5));
  REQUIRE(l.size() == 4);
  CHECK_EQ(l[0], std::string("VCC 0"));  // Cortex-M4 UDIV by zero yields 0
  CHECK(l[1].compare(0, 5, "TEMP ") == 0);
  CHECK_EQ(l[2], std::string("LED 3999L"));
}
