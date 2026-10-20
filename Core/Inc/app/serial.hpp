// Line-oriented serial output.
#pragma once

#include <cstddef>
#include <cstdint>

namespace app {

// Fixed-capacity text buffer for one output line; text beyond the capacity
// is dropped.
class Line {
 public:
  static constexpr std::size_t kCapacity = 32;

  Line() = default;
  explicit Line(const char* text) { *this << text; }

  Line& operator<<(const char* text);
  Line& operator<<(char c);
  Line& operator<<(uint32_t value);
  Line& operator<<(int32_t value);

  const char* data() const { return buffer_; }
  std::size_t size() const { return size_; }

 private:
  friend void sendLine(Line line);

  char buffer_[kCapacity + 2] = {};  // room for the line terminator
  std::size_t size_ = 0;
};

// Sends the line followed by "\r\n" in a single (blocking) transmission.
void sendLine(Line line);
void sendLine(const char* text);

}  // namespace app
