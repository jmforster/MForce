#pragma once
#include <charconv>
#include <string>

namespace mforce {

// The shortest decimal text that reads back to `v`: std::to_chars with no
// format argument, which writes the fewest digits that round-trip.
inline std::string float_text(float v) {
  char text[32];
  const auto end = std::to_chars(text, text + sizeof text, v).ptr;
  return std::string(text, end);
}

} // namespace mforce
