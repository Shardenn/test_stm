// Parser for the serial command protocol.
//
//   "?"      query VCC, temperature and both LED periods
//   "NNNNL"  set the LED ON period  (500..9999)
//   "NNNND"  set the LED OFF period (500..9999)
//
// Commands end with CR or LF; empty lines are ignored.
#pragma once

#include <cstddef>
#include <cstdint>

namespace app {

struct Command {
  enum class Type : uint8_t { None, Query, SetOnPeriod, SetOffPeriod, Invalid };

  Type type;
  uint32_t value;  // SetOnPeriod / SetOffPeriod only
};

class CommandParser {
 public:
  // Longest line kept; on the next character the buffer is discarded (that
  // character too) and collection starts over.
  static constexpr std::size_t kCapacity = 6;

  constexpr CommandParser() = default;

  // Feeds one received byte; returns the command once a line is complete,
  // Type::None otherwise.
  Command feed(char c);

 private:
  Command parse() const;

  char buffer_[kCapacity] = {};
  std::size_t length_ = 0;
};

}  // namespace app
