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
  // A pin's constant in the built graph, and how to write it: the Param whose
  // own number it is, or the ConstantSource wired to the pin (an Input, or a
  // Param of a type that keeps no Param object for it). Exactly one is set.
  struct Constant {
    std::string node, pin;
    Param* param{nullptr};                               // points into the node, which `nodes` owns
    std::shared_ptr<ConstantSource> source;
    void set(float v) const {
      if (param) param->constant = v;
      else source->set(v);
    }
  };
  // One per pin that holds a number, in the graph's node order, a node's by
  // pin name. A node a MultiplexSource's template reaches has none: its
  // clones are built apart, out of this list's reach.
  std::vector<Constant> constants;
  std::vector<std::string> warnings;                     // what a host should be told; the build prints nothing
};

} // namespace mforce
