#include "app/serial.hpp"

#include "app/board.hpp"

namespace app {

Line& Line::operator<<(const char* text) {
  while (*text != '\0') *this << *text++;
  return *this;
}

Line& Line::operator<<(char c) {
  if (size_ < kCapacity) buffer_[size_++] = c;
  return *this;
}

Line& Line::operator<<(uint32_t value) {
  char digits[10];
  std::size_t n = 0;
  do {
    digits[n++] = static_cast<char>('0' + value % 10U);
    value /= 10U;
  } while (value != 0U);
  while (n > 0U) *this << digits[--n];
  return *this;
}

Line& Line::operator<<(int32_t value) {
  if (value < 0) {
    *this << '-';
    return *this << (0U - static_cast<uint32_t>(value));
  }
  return *this << static_cast<uint32_t>(value);
}

void sendLine(Line line) {
  line.buffer_[line.size_++] = '\r';
  line.buffer_[line.size_++] = '\n';
  board::serialWrite(line.buffer_, line.size_);
}

void sendLine(const char* text) { sendLine(Line(text)); }

}  // namespace app
