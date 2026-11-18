#pragma once
#include "mforce/core/dsp_value_source.h"

namespace mforce {

// ---------------------------------------------------------------------------
// PerformNode: the declaration of a note field's node — which field of the
// note being played a graph reads. A voice's graph holds the voice's own
// adapter under this node's id, so no PerformNode object is in a graph; the
// type exists so the registry can say what the node declares.
// ---------------------------------------------------------------------------
struct PerformNode final : Generator {
  static constexpr const char* const kFieldLabels[] = {"frequency", "velocity", "wheel", "pressure",
                                                       "duration", "onset", nullptr};
  Setting<int> field{this, "field", 0, 0, 5, kFieldLabels};

  float compute() override { return 0.0f; }
  const char* type_name() const override { return "PerformNode"; }
};

} // namespace mforce
