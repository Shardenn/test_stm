// Requirement 1: autonomous LED cycle, 4 s on / 6 s off.
#include "helpers.hpp"

using namespace th;

TEST_CASE(led_cycles_4s_on_6s_off, "1.A-E") {
  boot();
  b().runFor(ms(64000));
  const auto edges = led4Edges();
  REQUIRE(edges.size() >= 12);

  // Characterisation: the firmware starts with the LED off and the first
  // OFF phase uses TIM3's initial period (4 s, from MX_TIM3_Init), not 6 s.
  CHECK(edges[0].on);
  CHECK_BETWEEN(edges[0].t, ms(4000), ms(4010));

  for (size_t i = 0; i + 1 < edges.size(); ++i) {
    CHECK_MSG(edges[i].on != edges[i + 1].on, "edges must alternate, index " << i);
    const Time d = edges[i + 1].t - edges[i].t;
    if (edges[i].on) CHECK_BETWEEN(d, ms(3995), ms(4005));
    else CHECK_BETWEEN(d, ms(5995), ms(6005));
  }
}

TEST_CASE(led_changes_and_durations_are_reported, "3.A") {
  boot();
  b().runFor(ms(21000));  // toggles at ~4, 8, 14 and 18 s
  const auto l = lines();
  REQUIRE(l.size() == 8);
  long long v = 0;
  // First report counts from power-up, so it includes the ~2.5 ms boot time.
  CHECK_MSG(numberAfter(l[0], "LED OFF TIME ", &v) && v >= 3995 && v <= 4010, l[0]);
  CHECK_EQ(l[1], std::string("LED ON"));
  CHECK_MSG(numberAfter(l[2], "LED ON TIME ", &v) && v >= 3995 && v <= 4005, l[2]);
  CHECK_EQ(l[3], std::string("LED OFF"));
  CHECK_MSG(numberAfter(l[4], "LED OFF TIME ", &v) && v >= 5995 && v <= 6005, l[4]);
  CHECK_EQ(l[5], std::string("LED ON"));
  CHECK_MSG(numberAfter(l[6], "LED ON TIME ", &v) && v >= 3995 && v <= 4005, l[6]);
  CHECK_EQ(l[7], std::string("LED OFF"));

  // "LED ON"/"LED OFF" is sent right after the pin actually changed.
  const auto txl = b().txLines();
  const auto edges = led4Edges();
  REQUIRE(edges.size() == 4);
  for (size_t i = 0; i < 4; ++i) {
    const emu::TxLine& state = txl[2 * i + 1];
    CHECK(state.start >= edges[i].t);
    CHECK(state.start - edges[i].t < ms(5));
    CHECK_EQ(state.text, std::string(edges[i].on ? "LED ON" : "LED OFF"));
  }
}
