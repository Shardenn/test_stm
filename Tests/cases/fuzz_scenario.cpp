// Randomised scenario for differential testing: the same seed drives the
// original C firmware and the C++ rewrite; their traces (every UART byte and
// LED edge with its timestamp) must be identical.
//
// Inputs stay inside the domain where the rewrite is meant to behave exactly
// like the original: the temperature reading is never below TS_CAL1 (the
// original's unsigned arithmetic wraps there - fixed in the rewrite) and the
// first ADC-dependent output happens after the 10-sample window is filled.
#include "fuzz_scenario.hpp"

#include "emulator.hpp"

#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace {

using emu::Time;
using emu::ms;
using emu::us;

struct Segment {
  Time until;
  uint16_t temp, vref;
};

std::vector<Segment> g_segments;

const Segment& segmentAt(Time t) {
  for (const Segment& s : g_segments) {
    if (t < s.until) return s;
  }
  return g_segments.back();
}

uint16_t noise(Time t, uint32_t salt) {
  uint64_t x = (t / us(1)) * 0x9E3779B97F4A7C15ULL + salt;
  x ^= x >> 29;
  x *= 0xBF58476D1CE4E5B9ULL;
  x ^= x >> 32;
  return static_cast<uint16_t>(x % 9);  // 0..8
}

std::string randomCommand(std::mt19937& rng) {
  auto pick = [&](int n) { return static_cast<int>(rng() % static_cast<unsigned>(n)); };
  std::string body;
  switch (pick(10)) {
    case 0: case 1:
      body = "?";
      break;
    case 2: case 3: case 4: {  // plausible value
      body = std::to_string(300 + pick(10000)) + (pick(2) ? "L" : "D");
      break;
    }
    case 5: {  // boundary values
      static const char* v[] = {"499", "500", "501", "9998", "9999", "10000", "0", "0500", "00999"};
      body = std::string(v[pick(9)]) + (pick(2) ? "L" : "D");
      break;
    }
    case 6: {  // random printable garbage, sometimes long
      const int n = 1 + pick(10);
      for (int i = 0; i < n; ++i) body += static_cast<char>(' ' + pick(95));
      break;
    }
    case 7: {  // random bytes including NUL and control characters
      const int n = 1 + pick(6);
      for (int i = 0; i < n; ++i) {
        char c = static_cast<char>(pick(256));
        if (c == '\r' || c == '\n') c = 'x';
        body += c;
      }
      break;
    }
    case 8: {  // digits with a wrong or missing suffix
      static const char* s[] = {"l", "d", "X", "", "LL", "?"};
      body = std::to_string(pick(20000)) + s[pick(6)];
      break;
    }
    default:
      body = pick(2) ? "?" : std::string("?") + static_cast<char>(pick(2) ? '\0' : '?');
      break;
  }
  static const char* term[] = {"\r", "\n", "\r\n", "\r\n", "\r", ""};
  return body + term[pick(6)];
}

}  // namespace

void runFuzzScenario(uint32_t seed) {
  std::mt19937 rng(seed);
  auto pick = [&](uint32_t lo, uint32_t hi) { return lo + rng() % (hi - lo + 1); };
  emu::Board& b = emu::board();
  const emu::Config& cfg = b.config();

  // Piecewise-constant analog inputs with a little deterministic noise.
  Time t = 0;
  const Time end = ms(pick(20000, 60000));
  while (t < end + ms(1000)) {
    t += ms(pick(50, 3000));
    g_segments.push_back({t, static_cast<uint16_t>(pick(cfg.tsCal1, cfg.tsCal1 + 600)),
                          static_cast<uint16_t>(pick(1300, 1900))});
  }
  b.config().tempRaw = [](Time at) { return static_cast<uint16_t>(segmentAt(at).temp + noise(at, 1)); };
  b.config().vrefRaw = [](Time at) { return static_cast<uint16_t>(segmentAt(at).vref + noise(at, 2)); };
  b.boot();

  b.runUntil(ms(300));
  while (b.now() < end) {
    switch (pick(0, 9)) {
      case 0: case 1: case 2: {  // clean press, sometimes around the 500 ms threshold
        const Time hold = pick(0, 3) == 0 ? ms(pick(490, 512)) : ms(pick(5, 1500));
        const Time t0 = b.now();
        b.scheduleButton(t0, true);
        b.scheduleButton(t0 + hold, false);
        if (pick(0, 3) == 0) b.uartSendAt(t0 + ms(pick(0, 400)), randomCommand(rng));
        b.runUntil(t0 + hold + ms(pick(1, 50)));
        break;
      }
      case 3: {  // bouncing contacts
        Time t0 = b.now();
        bool level = true;
        const int edges = static_cast<int>(pick(2, 9));
        for (int i = 0; i < edges; ++i) {
          b.scheduleButton(t0, level);
          level = !level;
          t0 += pick(0, 2) == 0 ? ms(pick(20, 800)) : us(pick(100, 25000));
        }
        b.scheduleButton(t0, false);
        b.runUntil(t0 + ms(5));
        break;
      }
      case 4: case 5: case 6: {  // a command, sometimes two back to back
        b.uartSend(randomCommand(rng));
        if (pick(0, 5) == 0) b.uartSend(randomCommand(rng));
        b.runFor(ms(pick(1, 300)));
        break;
      }
      default:  // let the LED cycle run
        b.runFor(ms(pick(10, 7000)));
        break;
    }
  }
  b.runFor(ms(100));
  std::fputs(b.trace().c_str(), stdout);
}
