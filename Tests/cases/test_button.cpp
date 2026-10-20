// Requirement 2: button actuations toggle the LED (press, press, release,
// release, ...), response <= 50 ms, timing restarts after a manual toggle.
// Requirement 3: button state and press duration are reported.
#include "helpers.hpp"

using namespace th;

TEST_CASE(first_press_toggles_led_and_reports_button, "2.A 3.A") {
  boot();
  b().runUntil(ms(1000));  // LED off, automatic switch-on due at ~4 s
  const Time t0 = b().now();
  b().setButton(true);
  b().runUntil(t0 + ms(100));

  const auto edges = led4Edges(t0);
  REQUIRE(edges.size() == 1);
  CHECK(edges[0].on);
  CHECK(edges[0].t - t0 <= ms(50));

  auto l = lines(t0);
  REQUIRE(l.size() == 3);
  long long v = 0;
  CHECK_MSG(numberAfter(l[0], "LED OFF TIME ", &v) && v >= 995 && v <= 1005, l[0]);
  CHECK_EQ(l[1], std::string("LED ON"));
  CHECK_EQ(l[2], std::string("BUTTON ON"));

  const Time t1 = b().now();
  b().setButton(false);
  b().runFor(ms(100));
  CHECK(led4Edges(t1).empty());  // the first release is not an actuation
  l = lines(t1);
  REQUIRE(l.size() == 2);
  CHECK_EQ(l[0], std::string("BUTTON OFF"));
  CHECK_MSG(numberAfter(l[1], "BUTTON TIME ", &v) && v >= 96 && v <= 100, l[1]);
}

TEST_CASE(actuation_pattern_is_press_press_release_release, "2.C") {
  boot();
  b().runUntil(ms(1000));
  bool expectedLed = false;
  for (int k = 0; k < 12; ++k) {
    const Time tp = b().now();
    b().setButton(true);
    b().runFor(ms(100));
    const Time tr = b().now();
    b().setButton(false);
    b().runFor(ms(400));

    const bool onPress = (k % 4) < 2;
    const auto atPress = b().ledEdges(Led::Led4, tp);
    size_t pressEdges = 0, releaseEdges = 0;
    for (const auto& e : atPress) (e.t < tr ? pressEdges : releaseEdges)++;
    CHECK_MSG(pressEdges == (onPress ? 1u : 0u), "cycle " << k);
    CHECK_MSG(releaseEdges == (onPress ? 0u : 1u), "cycle " << k);
    expectedLed = !expectedLed;
    CHECK_MSG(b().ledOn(Led::Led4) == expectedLed, "cycle " << k);
  }
}

TEST_CASE(response_to_actuation_within_50ms, "2.B") {
  boot();
  b().runUntil(ms(700));
  Time worst = 0;
  for (int k = 0; k < 8; ++k) {
    const Time tp = b().now();
    b().setButton(true);
    b().runFor(ms(60 + 20 * k));
    const Time tr = b().now();
    b().setButton(false);
    b().runFor(ms(300));
    const Time edgeTime = (k % 4) < 2 ? tp : tr;
    const auto* e = firstLed4EdgeAfter(led4Edges(), edgeTime);
    REQUIRE(e != nullptr);
    const Time latency = e->t - edgeTime;
    CHECK_MSG(latency <= ms(50), "actuation " << k << " latency " << emu::toMs(latency) << " ms");
    worst = std::max(worst, latency);
  }
  tf::log("worst actuation latency: " + std::to_string(emu::toMs(worst)) + " ms");
}

TEST_CASE(manual_toggle_restarts_the_on_off_timing, "2.D") {
  boot();
  // Manual ON at 1.5 s -> automatic OFF 4 s later (not at the original 8 s mark).
  b().runUntil(ms(1500));
  press(ms(100));
  const auto on = led4Edges(ms(1500));
  REQUIRE(on.size() == 1 && on[0].on);
  b().runUntil(ms(9000));
  auto e = led4Edges(on[0].t + 1);
  REQUIRE(!e.empty());
  CHECK(!e[0].on);
  CHECK_BETWEEN(e[0].t - on[0].t, ms(3995), ms(4005));

  // Second press (actuation on press) turns it ON again, third press acts on
  // its release and turns it OFF -> automatic ON 6 s after that release.
  b().runUntil(ms(9500));
  press(ms(100));      // ON
  b().runFor(ms(400));
  const Time t3 = b().now();
  press(ms(100));      // OFF on release
  const Time release = t3 + ms(100);
  b().runFor(ms(7000));
  e = led4Edges(release);
  REQUIRE(e.size() >= 2);
  CHECK(!e[0].on);
  CHECK(e[0].t - release <= ms(50));
  CHECK(e[1].on);
  CHECK_BETWEEN(e[1].t - e[0].t, ms(5995), ms(6005));
}

TEST_CASE(press_duration_is_reported_on_release, "3.A") {
  boot();
  b().runUntil(ms(1000));
  const Time t0 = b().now();
  press(ms(273));
  b().runFor(ms(50));
  const auto l = lines(t0);
  REQUIRE(l.size() == 5);
  CHECK_EQ(l[2], std::string("BUTTON ON"));
  CHECK_EQ(l[3], std::string("BUTTON OFF"));
  long long v = 0;
  CHECK_MSG(numberAfter(l[4], "BUTTON TIME ", &v) && v >= 268 && v <= 273, l[4]);
}

TEST_CASE(release_chatter_is_debounced, "2.A") {
  boot();
  b().runUntil(ms(1000));
  const Time t0 = b().now();
  b().setButton(true);
  b().runFor(ms(200));
  const Time t1 = b().now();
  // Contact bounce on release: 12 ms of chatter.
  b().scheduleButton(t1, false);
  b().scheduleButton(t1 + ms(2), true);
  b().scheduleButton(t1 + ms(5), false);
  b().scheduleButton(t1 + ms(7), true);
  b().scheduleButton(t1 + ms(12), false);
  b().runFor(ms(300));

  const auto l = lines(t0);
  REQUIRE(l.size() == 5);
  CHECK_EQ(l[2], std::string("BUTTON ON"));
  CHECK_EQ(l[3], std::string("BUTTON OFF"));
  CHECK_EQ(led4Edges(t0).size(), 1u);

  // The next real press is recognised as the second actuation.
  const Time t2 = b().now();
  press(ms(100));
  b().runFor(ms(50));
  CHECK_EQ(led4Edges(t2).size(), 1u);
  CHECK(!b().ledOn(Led::Led4));
}

TEST_CASE(press_chatter_is_not_filtered_characterization, "2.A") {
  // Characterisation of the original firmware: only the release->press
  // direction is debounced.  A press edge that bounces after the press was
  // handled is taken as a short press, and the real press that follows within
  // 20 ms is ignored.  (Bounces that arrive while the press interrupt is still
  // printing collapse into one pending EXTI and are harmless.  The
  // STM32F411E-DISCO user button also has an RC filter.)
  boot();
  b().runUntil(ms(1000));
  const Time t0 = b().now();
  b().scheduleButton(t0, true);
  b().scheduleButton(t0 + ms(5), false);
  b().scheduleButton(t0 + ms(8), true);
  b().scheduleButton(t0 + ms(300), false);
  b().runFor(ms(400));
  const auto l = lines(t0);
  REQUIRE(l.size() == 5);
  CHECK_EQ(l[2], std::string("BUTTON ON"));
  CHECK_EQ(l[3], std::string("BUTTON OFF"));
  long long v = 0;
  CHECK_MSG(numberAfter(l[4], "BUTTON TIME ", &v) && v <= 5, l[4]);
  CHECK_EQ(led4Edges(t0).size(), 1u);
}
