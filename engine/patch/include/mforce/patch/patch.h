#pragma once
#include <optional>
#include <string>
#include <vector>
#include "mforce/patch/graph.h"
#include "mforce/patch/instrument_block.h"
#include "mforce/patch/layout.h"

namespace mforce {

// A patch as data: its graph, its instrument block or none, its layout, and
// the standalone render's rate, length and score.
struct Patch {
  Graph graph;
  std::optional<InstrumentBlock> instrument;             // present: an instrument; absent: a plain sound
  Layout layout;
  int sampleRate{48000};
  std::optional<double> seconds;
  std::string score;                                     // the embedded score as JSON text this module never reads; empty when the file has none
  std::string comment;
  bool operator==(const Patch&) const = default;

  // Every way this patch breaks what a patch keeps true, each as
  // "<path>: <sentence>"; empty when it holds. Reads the declarations table.
  std::vector<std::string> check() const;
};

} // namespace mforce
