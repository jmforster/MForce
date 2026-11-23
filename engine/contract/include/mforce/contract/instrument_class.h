#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace mforce {

// What a performer may know about an instrument, filled from the patch's
// instrument block. `sustaining` is declared by the patch's author, never
// inferred: phrasing (hold, onset gestures, glide) applies only when it is
// true. `onsets` is the instrument's onset vocabulary, in declaration order.
struct InstrumentClass {
  bool sustaining{false};
  std::vector<std::string> onsets;            // ordered

  // The name's position in `onsets` plus one; 0 for an empty name or a name
  // not in the list.
  int onset_id(std::string_view name) const {
    if (name.empty()) return 0;
    for (std::size_t k = 0; k < onsets.size(); ++k)
      if (onsets[k] == name) return int(k + 1);
    return 0;
  }
};

} // namespace mforce
