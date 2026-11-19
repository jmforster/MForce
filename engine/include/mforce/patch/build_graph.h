#pragma once
#include <string>
#include <vector>
#include "mforce/patch/built_graph.h"
#include "mforce/patch/graph.h"
#include "mforce/patch/instrument_block.h"
#include "mforce/patch/note_signals.h"

namespace mforce {

// Builds `graph`. A null `note` is a plain sound, where a PerformNode fails the
// build; a null `instrument` gives a NameGate no vocabulary. Throws
// std::runtime_error naming the node, the pin and the reason.
BuiltGraph build_graph(const Graph& graph, const NoteSignals* note, const InstrumentBlock* instrument);
// The ids of the nodes only taps read (and that are not the output), in the
// graph's node order: what a voice must pull once per sample after its output.
std::vector<std::string> advance_ids(const Graph& graph);

} // namespace mforce
