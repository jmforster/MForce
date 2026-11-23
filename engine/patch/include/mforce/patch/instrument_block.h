#pragma once
#include <string>
#include <vector>

namespace mforce {

// What makes a patch an instrument: the voice pool's size, the instrument's
// volume, whether a note sustains, the onset vocabulary and the glide time.
struct InstrumentBlock {
  int polyphony{4};
  float volume{1.0f};
  bool sustaining{false};
  std::vector<std::string> onsets;                       // ordered: an onset's id is its position, 0 is none
  float glideMs{15.0f};
  bool operator==(const InstrumentBlock&) const = default;
};

} // namespace mforce
