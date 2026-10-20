#include "app/command_parser.hpp"

#include "app/config.hpp"

namespace app {
namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

}  // namespace

Command CommandParser::feed(char c) {
  if (c == '\r' || c == '\n') {
    if (length_ == 0) return {Command::Type::None, 0};
    const Command command = parse();
    length_ = 0;
    return command;
  }
  if (length_ < kCapacity) {
    buffer_[length_++] = c;
  } else {
    length_ = 0;
  }
  return {Command::Type::None, 0};
}

Command CommandParser::parse() const {
  const Command invalid = {Command::Type::Invalid, 0};

  // A NUL right after '?' ends the string for the C string comparison the
  // protocol was first implemented with, so it is accepted as a query.
  if (buffer_[0] == '?' && (length_ == 1 || buffer_[1] == '\0')) return {Command::Type::Query, 0};
  if (length_ < 2) return invalid;

  const char suffix = buffer_[length_ - 1];
  if (suffix != 'L' && suffix != 'D') return invalid;

  uint32_t value = 0;
  for (std::size_t i = 0; i + 1 < length_; ++i) {
    if (!isDigit(buffer_[i])) return invalid;
    value = value * 10U + static_cast<uint32_t>(buffer_[i] - '0');  // at most 5 digits
  }
  if (value < config::kLedPeriodMin || value > config::kLedPeriodMax) return invalid;
  return {suffix == 'L' ? Command::Type::SetOnPeriod : Command::Type::SetOffPeriod, value};
}

}  // namespace app
