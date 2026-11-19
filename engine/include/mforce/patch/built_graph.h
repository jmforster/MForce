#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "mforce/core/dsp_value_source.h"

namespace mforce {

// A graph built: its nodes wired and checked.
struct BuiltGraph {
  std::unordered_map<std::string, std::shared_ptr<Component>> nodes;   // every node by id, signal or part
  std::shared_ptr<Component> output;                     // the output node: a signal, or the mixer part of a plain sound
  struct Signal { std::string id; ValueSource* node; };
  std::vector<Signal> signals;                           // the signals among the nodes, in id order
  std::vector<std::shared_ptr<ValueSource>> advance;     // the tap-only tails, in the graph's node order
  struct DynamicPin { std::shared_ptr<Component> node; std::string setting; std::shared_ptr<ValueSource> source; };
  std::vector<DynamicPin> dynamicPins;                   // in the graph's node order, a node's by setting name
  std::vector<std::string> warnings;                     // what a host should be told; the build prints nothing
};

} // namespace mforce
