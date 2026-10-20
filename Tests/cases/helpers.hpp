// Helpers shared by the emulator test cases.
#pragma once

#include "emulator.hpp"
#include "test_framework.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace th {

using emu::Led;
using emu::Time;
using emu::ms;
using emu::us;

inline emu::Board& b() { return emu::board(); }

// Boots the firmware with the default analog inputs.
inline void boot() { b().boot(); }

// Text of all complete output lines starting at or after `from`.
inline std::vector<std::string> lines(Time from = 0) {
  std::vector<std::string> out;
  for (const emu::TxLine& l : b().txLines(from)) out.push_back(l.text);
  return out;
}

inline std::string joined(const std::vector<std::string>& v) {
  std::string s;
  for (const std::string& l : v) s += "  | " + l + "\n";
  return s;
}

// True if `prefix` starts `line`; stores the decimal number that follows.
inline bool numberAfter(const std::string& line, const std::string& prefix, long long* value) {
  if (line.compare(0, prefix.size(), prefix) != 0) return false;
  const std::string rest = line.substr(prefix.size());
  if (rest.empty()) return false;
  size_t i = 0;
  if (rest[0] == '-') i = 1;
  if (i == rest.size()) return false;
  for (size_t k = i; k < rest.size(); ++k) {
    if (rest[k] < '0' || rest[k] > '9') return false;
  }
  *value = std::stoll(rest);
  return true;
}

// Sends `cmd` to the MCU and returns the lines it printed within `wait`.
inline std::vector<std::string> command(const std::string& cmd, Time wait = ms(30)) {
  const Time t0 = b().now();
  b().uartSend(cmd);
  b().runFor(wait);
  return lines(t0);
}

// Holds the button for `duration` (measured from the press edge), then
// releases it.
inline void press(Time duration) {
  const Time t0 = b().now();
  b().setButton(true);
  b().runUntil(t0 + duration);
  b().setButton(false);
}

// Edges of LED4 (the LED the application drives) since `from`.
inline std::vector<emu::LedEdge> led4Edges(Time from = 0) { return b().ledEdges(Led::Led4, from); }

// First LED4 edge at or after `t`, or nullptr.
inline const emu::LedEdge* firstLed4EdgeAfter(const std::vector<emu::LedEdge>& edges, Time t) {
  for (const emu::LedEdge& e : edges) {
    if (e.t >= t) return &e;
  }
  return nullptr;
}

// Expected integer conversions, written independently of the firmware from
// the reference manual / datasheet formulas (with C integer semantics).
inline long long expectedVcc(uint32_t avgVref, uint16_t vrefintCal) {
  return avgVref == 0 ? 0 : 3300LL * vrefintCal / avgVref;
}
inline long long expectedTemp(long long avgTemp, uint16_t cal1, uint16_t cal2) {
  return (avgTemp - cal1) * (110 - 30) / (static_cast<long long>(cal2) - cal1) + 30;
}

}  // namespace th
