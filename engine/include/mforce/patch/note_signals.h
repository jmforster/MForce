#pragma once
#include <memory>
#include <stdexcept>
#include <string>
#include "mforce/core/dsp_value_source.h"

namespace mforce {

// The six signals of the note a graph is built for, in
// PerformNode::kFieldLabels' order.
struct NoteSignals {
  std::shared_ptr<ValueSource> frequency, velocity, wheel, pressure, duration, onset;
  const std::shared_ptr<ValueSource>& field(int index) const;   // 0..5, the label's index
};

// The member a label's index names; an index outside 0..5 throws std::out_of_range.
inline const std::shared_ptr<ValueSource>& NoteSignals::field(int index) const {
  switch (index) {
    case 0: return frequency;
    case 1: return velocity;
    case 2: return wheel;
    case 3: return pressure;
    case 4: return duration;
    case 5: return onset;
    default: throw std::out_of_range("a note field's index is 0 to 5, not " + std::to_string(index));
  }
}

} // namespace mforce
