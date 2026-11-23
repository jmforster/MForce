#pragma once
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace mforce {

// The small words every refusal of the module is built from: a name in
// quotes, a list joined by commas, and whether a stored number is an index
// into a list of labels.

// `s` in single quotes: 'cutoff'.
inline std::string in_quotes(std::string_view s) { return "'" + std::string(s) + "'"; }

// The words joined by ", ".
inline std::string comma_list(const std::vector<std::string>& words) {
  std::string out;
  for (const auto& w : words) out += (out.empty() ? "" : ", ") + w;
  return out;
}

// True when `v` is a whole number from 0 to below `count`.
inline bool is_index(float v, size_t count) { return v >= 0.0f && v == std::floor(v) && v < float(count); }

} // namespace mforce
