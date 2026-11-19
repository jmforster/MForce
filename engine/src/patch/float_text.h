#pragma once
#include <bit>
#include <charconv>
#include <cstdint>
#include <string>

namespace mforce {

// The shortest decimal text that reads back to `v`: std::to_chars with no
// format argument, which writes the fewest digits that round-trip.
inline std::string float_text(float v) {
  char text[32];
  const auto end = std::to_chars(text, text + sizeof text, v).ptr;
  return std::string(text, end);
}

// The number a patch file holds for `v`: the double nearest float_text(v),
// read by std::from_chars, when that double narrows back to `v` bit for bit;
// otherwise `v` widened to a double exactly, which narrows back to `v` and
// which the JSON library prints as that double's own shortest text.
inline double float_number(float v) {
  const std::string text = float_text(v);
  double d = 0.0;
  std::from_chars(text.data(), text.data() + text.size(), d);
  return std::bit_cast<uint32_t>(float(d)) == std::bit_cast<uint32_t>(v) ? d : double(v);
}

} // namespace mforce
