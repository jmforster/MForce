#include "mforce/patch/build_graph.h"
#include "mforce/core/source_registry.h"
#include "mforce/patch/table_rule.h"
#include "mforce/patch/type_declarations.h"
#include "mforce/source/combiner/multiplex_source.h"
#include "mforce/source/modulator/name_gate.h"
#include "mforce/source/modulator/perform_node.h"
#include "float_text.h"
#include "graph_cycles.h"
#include "sentence.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mforce {

namespace {

std::string opening(const std::string& id, std::string_view type) {
  return "node " + in_quotes(id) + " (" + std::string(type) + "): ";
}

// A tap's placeholder and the id it reads, bound once every node is built.
struct TapBind {
  std::shared_ptr<TapSource> tap;
  std::string target;
};

// One build's inputs and what it has made so far.
struct Build {
  const Graph& graph;
  const NoteSignals* note;
  const InstrumentBlock* instrument;
  std::map<std::string, size_t, std::less<>> index;      // an id's first node
  std::vector<TapBind> taps;
  BuiltGraph out;

  const Node* node(std::string_view id) const {
    auto it = index.find(id);
    return it == index.end() ? nullptr : &graph.nodes[it->second];
  }
};

// A signal where one is required: a tap's target, an output, a dynamic pin's
// source. A part there is refused as one; a null node stays null.
std::shared_ptr<ValueSource> as_signal(const std::shared_ptr<Component>& node, const std::string& where) {
  auto signal = std::dynamic_pointer_cast<ValueSource>(node);
  if (!signal && node)
    throw std::runtime_error(in_quotes(where) + " is a " + node->type_name() + ", a part, not a signal");
  return signal;
}

// ---------------------------------------------------------------------------
// Step 1: the order of construction.

// The ids a node needs built before it: its feeds that are not taps and its
// dynamic pins' sources. A feed or a dynamic pin that names no node is refused.
std::vector<std::string> needs(const Build& b, const Node& n) {
  std::vector<std::string> ids;
  for (const auto& [pin, feeds] : n.wires)
    for (const auto& w : feeds) {
      if (w.tap) continue;
      if (!b.node(w.source)) throw std::runtime_error("Unresolved node ref: " + w.source);
      ids.push_back(w.source);
    }
  for (const auto& [key, source] : n.dynamicPins) {
    if (!b.node(source))
      throw std::runtime_error("wiring: '" + n.id + "." + key + "' refs unknown node '" + source + "'");
    ids.push_back(source);
  }
  return ids;
}

// At each step the first node in the graph's order whose needs are all built.
// When nodes remain and none can be taken, the cycle is refused by name.
std::vector<size_t> build_order(const Build& b) {
  const auto& nodes = b.graph.nodes;
  std::vector<std::vector<std::string>> need(nodes.size());
  for (size_t i = 0; i < nodes.size(); ++i) need[i] = needs(b, nodes[i]);
  std::vector<bool> taken(nodes.size(), false);
  std::set<std::string, std::less<>> built;
  std::vector<size_t> order;
  while (order.size() < nodes.size()) {
    size_t next = nodes.size();
    for (size_t i = 0; i < nodes.size() && next == nodes.size(); ++i)
      if (!taken[i] && std::all_of(need[i].begin(), need[i].end(),
                                   [&](const std::string& id) { return built.count(id) != 0; }))
        next = i;
    if (next == nodes.size()) {
      const auto cycles = untapped_cycles(b.graph);
      throw std::runtime_error(cycles.empty() ? "the graph's reads close a cycle with no tap in it" : cycles.front());
    }
    taken[next] = true;
    built.insert(nodes[next].id);
    order.push_back(next);
  }
  return order;
}

// ---------------------------------------------------------------------------
// Step 2: each node created and filled through the by-name doors.

// A wire the node will not take: a pin it does not have, or a part of the
// wrong kind for a slot.
[[noreturn]] void reject_wire(const Component& node, const std::string& id, std::string_view pin,
                              const Component& part, bool isSignal) {
  std::string takes;
  for (const auto& d : node.input_descriptors())
    if (d.slot && d.hint && pin == d.name) takes = "; '" + std::string(pin) + "' takes " + d.hint;
  throw std::runtime_error(opening(id, node.type_name()) + "'" + std::string(pin) + "' does not accept a " +
                           part.type_name() + (isSignal ? "" : ", a part, not a signal") + takes);
}

// The node a feed reads; a tap is a placeholder, bound after the last node.
// `isSignal` says whether what it returns is a signal.
std::shared_ptr<Component> resolve(Build& b, const Wire& w, bool& isSignal) {
  isSignal = true;
  if (w.tap) {
    auto tap = std::make_shared<TapSource>();
    b.taps.push_back({tap, w.source});
    return tap;
  }
  auto it = b.out.nodes.find(w.source);
  if (it == b.out.nodes.end()) throw std::runtime_error("Unresolved node ref: " + w.source);
  isSignal = dynamic_cast<const ValueSource*>(it->second.get()) != nullptr;
  return it->second;
}

void wire(Component& c, const std::string& id, const char* pin, const std::shared_ptr<Component>& part, bool add,
          bool isSignal) {
  if (!(add ? c.add_param(pin, part) : c.set_param(pin, part))) reject_wire(c, id, pin, *part, isSignal);
}

// One feed wired: set_param, or add_param on a list.
void wire_feed(Build& b, Component& c, const std::string& id, const char* pin, const Wire& w, bool add) {
  bool isSignal = true;
  const auto part = resolve(b, w, isSignal);
  wire(c, id, pin, part, add, isSignal);
}

// Inputs: a number is a ConstantSource wired in; a list pin is cleared and
// each feed added in order; any other pin takes its feed through set_param.
void fill_inputs(Build& b, Component& c, const Node& n) {
  for (const auto& d : c.input_descriptors()) {
    if (auto v = n.values.find(d.name); v != n.values.end())
      wire(c, n.id, d.name, std::make_shared<ConstantSource>(v->second), false, true);
    auto w = n.wires.find(d.name);
    if (w == n.wires.end()) continue;
    if (d.multi) c.clear_param(d.name);
    for (const auto& feed : w->second) wire_feed(b, c, n.id, d.name, feed, d.multi);
  }
}

// Params: a number is the pin's own constant and wires nothing; a feed is
// wired through set_param.
void fill_params(Build& b, Component& c, const Node& n) {
  for (const auto& d : c.param_descriptors()) {
    if (auto v = n.values.find(d.name); v != n.values.end()) {
      if (Param* p = c.param(d.name)) p->constant = v->second;
      else wire(c, n.id, d.name, std::make_shared<ConstantSource>(v->second), false, true);
    }
    if (auto w = n.wires.find(d.name); w != n.wires.end())
      for (const auto& feed : w->second) wire_feed(b, c, n.id, d.name, feed, false);
  }
}

// Settings in declaration order, a string through set_string; then arrays;
// then the table rule over the arrays the node now holds from the record.
void fill_settings_and_arrays(Component& c, const Node& n) {
  for (const auto& d : c.setting_descriptors()) {
    if (d.type == SettingType::String) {
      if (auto s = n.strings.find(d.name); s != n.strings.end()) c.set_string(d.name, s->second);
      continue;
    }
    if (auto s = n.settings.find(d.name); s != n.settings.end()) c.set_setting(d.name, s->second);
  }
  for (const auto& d : c.array_descriptors())
    if (auto a = n.arrays.find(d.name); a != n.arrays.end()) c.set_array(d.name, a->second);
  const std::string ragged = ragged_table_refusal(c, [&](const char* column) {
    auto it = n.arrays.find(column);
    return it == n.arrays.end() ? -1 : int(it->second.size());
  });
  if (!ragged.empty()) throw std::runtime_error(opening(n.id, c.type_name()) + ragged);
}

// A PerformNode creates nothing: its id is the note's signal its field names.
void take_note_field(Build& b, const Node& n) {
  if (!b.note) throw std::runtime_error(opening(n.id, n.type) + "a plain sound has no note to read");
  const auto f = n.settings.find("field");
  const float index = f == n.settings.end() ? 0.0f : f->second;
  if (!is_index(index, 6)) {
    std::vector<std::string> labels;
    for (int k = 0; PerformNode::kFieldLabels[k]; ++k) labels.emplace_back(PerformNode::kFieldLabels[k]);
    throw std::runtime_error(opening(n.id, n.type) + "'field' is " + float_text(index) +
                             ", which is not an index into its labels (" + comma_list(labels) + ")");
  }
  b.out.nodes[n.id] = b.note->field(int(index));
}

// A NameGate's name against the block's onsets, 1-based; a name that finds
// no place is one warning.
void resolve_name(Build& b, NameGate& gate, const Node& n) {
  const std::string name = gate.get_string("name");
  if (b.instrument)
    for (size_t k = 0; k < b.instrument->onsets.size(); ++k)
      if (b.instrument->onsets[k] == name) {
        gate.targetId = float(k + 1);
        break;
      }
  if (name.empty() || gate.targetId >= 0.0f) return;
  b.out.warnings.push_back(opening(n.id, n.type) + "the name " + in_quotes(name) +
                           (b.instrument ? " is not one of the instrument's onsets"
                                         : " finds no onsets: the graph is built without an instrument block") +
                           ", so the gate never fires");
}

// ---------------------------------------------------------------------------
// The template a MultiplexSource clones: every node its root reaches over
// wires, taps and dynamic pins, in the graph's order, its output the root.

Graph template_of(const Build& b, const Node& mux, const std::string& root) {
  std::set<std::string, std::less<>> reached;
  std::vector<std::string> pending{root};
  while (!pending.empty()) {
    const std::string id = pending.back();
    pending.pop_back();
    const Node* n = b.node(id);
    if (!n || !reached.insert(id).second) continue;
    for (const auto& [pin, feeds] : n->wires)
      for (const auto& w : feeds) pending.push_back(w.source);
    for (const auto& [key, source] : n->dynamicPins) pending.push_back(source);
  }
  Graph sub;
  for (const auto& n : b.graph.nodes)
    if (reached.count(n.id)) sub.nodes.push_back(n);
  sub.output = root;
  const std::string of = " the template of " + in_quotes(mux.id);
  for (const auto& n : sub.nodes)
    if (!n.dynamicPins.empty())
      throw std::runtime_error(opening(n.id, n.type) + "a dynamic pin inside" + of +
                               " is not supported: each clone would need its own");
  const std::vector<std::string> unadvanced = advance_ids(sub);
  if (!unadvanced.empty()) {
    const std::string& id = unadvanced.front();
    const Node* n = b.node(id);
    throw std::runtime_error(opening(id, n->type) + "inside" + of +
                             " only a tap reads it, and nothing advances a clone's tail");
  }
  return sub;
}

// Clone `i` of a template: every node's seed XOR-ed with the clone's
// perturbation, which is injected where a node has none.
std::shared_ptr<ValueSource> build_clone(const Graph& sub, uint32_t base, int i,
                                         const std::optional<NoteSignals>& note,
                                         const std::optional<InstrumentBlock>& block) {
  const uint32_t perturbation = base ^ (uint32_t(i) * 0x9E3779B9u);
  Graph g = sub;
  for (auto& n : g.nodes) n.seed = n.seed ? (*n.seed ^ perturbation) : perturbation;
  BuiltGraph built = build_graph(g, note ? &*note : nullptr, block ? &*block : nullptr);
  return as_signal(built.output, g.output);
}

// A MultiplexSource whose source is one feed that is not a tap takes a
// builder holding, by value, the template, the note's signals and the block.
void give_template(Build& b, MultiplexSource& mux, const Node& n) {
  auto w = n.wires.find("source");
  if (w == n.wires.end() || w->second.size() != 1 || w->second.front().tap) return;
  Graph sub = template_of(b, n, w->second.front().source);
  const Node* root = b.node(sub.output);
  const uint32_t base = root && root->seed ? *root->seed : 0u;
  std::optional<NoteSignals> note;
  if (b.note) note = *b.note;
  std::optional<InstrumentBlock> block;
  if (b.instrument) block = *b.instrument;
  mux.set_template([sub = std::move(sub), base, note = std::move(note), block = std::move(block)](int i) {
    return build_clone(sub, base, i, note, block);
  });
}

// The node's own check of itself, filled: a refusal fails the build, each
// warning is kept.
void check_filled(Build& b, const Component& c, const std::string& id) {
  if (const std::string problem = c.validate(); !problem.empty())
    throw std::runtime_error(opening(id, c.type_name()) + problem);
  for (const auto& text : c.warnings()) b.out.warnings.push_back(opening(id, c.type_name()) + text);
}

// The object the registry made for one of the builder's names, as its class;
// an object of another class is refused naming the node.
template <class T>
T& named_as(const std::shared_ptr<Component>& c, const Node& n) {
  auto* t = dynamic_cast<T*>(c.get());
  if (!t) throw std::runtime_error(opening(n.id, n.type) + "the registry's " + n.type + " is not a " + n.type);
  return *t;
}

void create_node(Build& b, const Node& n) {
  if (n.type == "PerformNode") {
    take_note_field(b, n);
    return;
  }
  if (!type_declarations(n.type)) {
    std::string msg = "Unknown node type: " + n.type;
    if (const char* hint = retired_type_hint(n.type)) msg += std::string("; ") + hint;
    throw std::runtime_error(msg);
  }
  std::shared_ptr<Component> c = SourceRegistry::instance().create(n.type, n.seed);
  fill_inputs(b, *c, n);
  fill_params(b, *c, n);
  fill_settings_and_arrays(*c, n);
  if (n.type == "NameGate") resolve_name(b, named_as<NameGate>(c, n), n);
  if (n.type == "MultiplexSource") give_template(b, named_as<MultiplexSource>(c, n), n);
  b.out.nodes[n.id] = c;
  check_filled(b, *c, n.id);
}

// ---------------------------------------------------------------------------
// Steps 3 and 4: the taps' targets; the dynamic pins.

void bind_taps(Build& b) {
  for (auto& t : b.taps) {
    auto it = b.out.nodes.find(t.target);
    if (it == b.out.nodes.end()) throw std::runtime_error("Unresolved tap target: " + t.target);
    t.tap->source = as_signal(it->second, t.target);
  }
}

void bind_dynamic_pins(Build& b) {
  for (const auto& n : b.graph.nodes)
    for (const auto& [key, source] : n.dynamicPins) {
      const auto& consumer = b.out.nodes.at(n.id);
      const std::string where = "wiring: '" + n.id + ".dynamicPins." + key + "'";
      const std::string type = consumer ? consumer->type_name() : n.type;
      const SettingDescriptor* setting = nullptr;
      if (consumer)
        for (const auto& d : consumer->setting_descriptors())
          if (key == d.name) setting = &d;
      if (!setting) throw std::runtime_error(where + " is not a setting on " + type + " (fixed pins belong in \"params\")");
      if (setting->structural)
        throw std::runtime_error(where + " is a structural setting on " + type +
                                 " (it sizes or reshapes the node at prepare), so it takes no pin");
      b.out.dynamicPins.push_back({consumer, key, as_signal(b.out.nodes.at(source), n.id + ".dynamicPins." + key)});
    }
}

// ---------------------------------------------------------------------------
// Step 5: the bend's reach, the tails, the output, the signals.

// Whether a frequency PerformNode reaches `id` over wires that are not taps.
bool reaches_frequency(const Build& b, const std::string& id, std::map<std::string, bool>& memo) {
  if (auto m = memo.find(id); m != memo.end()) return m->second;
  memo[id] = false;                                      // a node on the walk reads as not reaching
  const Node* n = b.node(id);
  if (!n) return false;
  if (n->type == "PerformNode") {
    const auto f = n->settings.find("field");
    return memo[id] = (f == n->settings.end() || f->second == 0.0f);
  }
  for (const auto& [pin, feeds] : n->wires)
    for (const auto& w : feeds)
      if (!w.tap && reaches_frequency(b, w.source, memo)) return memo[id] = true;
  return false;
}

// A node that reads its frequency once at note start, whose `frequency` pin a
// frequency PerformNode reaches, cannot follow a bend: one warning each.
void warn_bend(Build& b) {
  std::map<std::string, bool> memo;
  for (const auto& n : b.graph.nodes) {
    auto it = b.out.nodes.find(n.id);
    const auto* signal = it == b.out.nodes.end() ? nullptr : dynamic_cast<const ValueSource*>(it->second.get());
    if (!signal || signal->tracks_frequency_live()) continue;      // a part has no frequency to leave behind
    auto w = n.wires.find("frequency");
    if (w == n.wires.end() || w->second.size() != 1 || w->second.front().tap) continue;
    if (reaches_frequency(b, w->second.front().source, memo))
      b.out.warnings.push_back(opening(n.id, it->second->type_name()) +
                               "it reads its frequency once at note start, so a bend does not reach it");
  }
}

void finish(Build& b) {
  warn_bend(b);
  for (const auto& id : advance_ids(b.graph)) b.out.advance.push_back(as_signal(b.out.nodes.at(id), id));
  auto out = b.out.nodes.find(b.graph.output);
  if (out == b.out.nodes.end())
    throw std::runtime_error(b.note ? "instrument: output node '" + b.graph.output + "' not found"
                                    : "graph.output must be a StereoMixer or a mono-renderable source: " + b.graph.output);
  b.out.output = out->second;
  for (const auto& [id, node] : b.out.nodes)
    if (auto* s = dynamic_cast<ValueSource*>(node.get())) b.out.signals.push_back({id, s});
  std::sort(b.out.signals.begin(), b.out.signals.end(), [](const auto& x, const auto& y) { return x.id < y.id; });
}

} // namespace

BuiltGraph build_graph(const Graph& graph, const NoteSignals* note, const InstrumentBlock* instrument) {
  if (graph.output.empty()) throw std::runtime_error("the patch has no output");
  Build b{graph, note, instrument, {}, {}, {}};
  for (size_t i = 0; i < graph.nodes.size(); ++i)
    if (!b.index.try_emplace(graph.nodes[i].id, i).second)
      throw std::runtime_error(opening(graph.nodes[i].id, graph.nodes[i].type) + "the id " +
                               in_quotes(graph.nodes[i].id) + " names an earlier node too; an id names one node");
  for (size_t i : build_order(b)) create_node(b, graph.nodes[i]);
  bind_taps(b);
  bind_dynamic_pins(b);
  finish(b);
  return std::move(b.out);
}

std::vector<std::string> advance_ids(const Graph& graph) {
  std::set<std::string, std::less<>> normal, tapped;
  for (const auto& n : graph.nodes)
    for (const auto& [pin, feeds] : n.wires)
      for (const auto& w : feeds) (w.tap ? tapped : normal).insert(w.source);
  std::vector<std::string> ids;
  for (const auto& n : graph.nodes)
    if (tapped.count(n.id) && !normal.count(n.id) && n.id != graph.output) ids.push_back(n.id);
  return ids;
}

} // namespace mforce
