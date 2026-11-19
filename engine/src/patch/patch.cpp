#include "mforce/patch/patch.h"
#include "mforce/core/source_registry.h"
#include "mforce/patch/table_rule.h"
#include "mforce/patch/type_declarations.h"
#include "float_text.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace mforce {

namespace {

std::string in_quotes(std::string_view s) { return "'" + std::string(s) + "'"; }

std::string joined(const std::vector<std::string>& words) {
  std::string out;
  for (const auto& w : words) out += (out.empty() ? "" : ", ") + w;
  return out;
}

// True when `v` is a whole number from 0 to below `count`.
bool is_index(float v, size_t count) {
  return v >= 0.0f && v == std::floor(v) && v < float(count);
}

std::string node_path(size_t i) { return "graph.nodes[" + std::to_string(i) + "]"; }

// What a node's name means for its pins: a Param, or an Input of any sort.
struct Pin {
  bool declared{false};
  bool list{false};
  const TypeDeclarations::Input* slot{nullptr};          // the slot's declaration when the pin is one
};

Pin pin_of(const TypeDeclarations& d, std::string_view name) {
  if (d.param(name)) return {true, false, nullptr};
  if (const auto* in = d.input(name)) return {true, in->list, in->slot ? in : nullptr};
  return {};
}

// The violations found so far, and the patch's nodes by id: an id's first
// node, which is the one a feed of that id reads.
struct Checker {
  const Patch& patch;
  std::map<std::string, size_t, std::less<>> index;
  std::vector<std::string> lines;

  explicit Checker(const Patch& p) : patch(p) {
    for (size_t i = 0; i < p.graph.nodes.size(); ++i) index.try_emplace(p.graph.nodes[i].id, i);
  }
  void add(const std::string& path, const std::string& sentence) { lines.push_back(path + ": " + sentence); }
  const Node* node(std::string_view id) const {
    auto it = index.find(id);
    return it == index.end() ? nullptr : &patch.graph.nodes[it->second];
  }
};

// One node's place in the record, its opening words and its declarations.
struct NodeAt {
  const Node& node;
  std::string path;
  std::string opening;
  const TypeDeclarations& d;
  std::string param_path(const std::string& name) const { return path + ".params." + name; }
};

// Ids non-empty; one namespace for node ids, group names and face labels,
// each name refused once, at the second thing's path.
void check_names(Checker& c) {
  std::map<std::string, const char*> first;
  std::set<std::string> refused;
  const auto claim = [&](const std::string& name, const char* kind, const std::string& path) {
    const auto [it, fresh] = first.try_emplace(name, kind);
    if (fresh || !refused.insert(name).second) return;
    c.add(path, in_quotes(name) + " names two things: " + it->second + ", " + kind +
                "; ids, group names and face labels share one namespace");
  };
  const auto& nodes = c.patch.graph.nodes;
  for (size_t i = 0; i < nodes.size(); ++i) {
    if (nodes[i].id.empty()) c.add(node_path(i) + ".id", "an id is empty");
    else claim(nodes[i].id, "a node", node_path(i) + ".id");
  }
  const auto& groups = c.patch.layout.groups;
  for (size_t g = 0; g < groups.size(); ++g)
    claim(groups[g].name, "a group", "layout.groups[" + std::to_string(g) + "].name");
  const auto& faces = c.patch.layout.faces;
  for (size_t f = 0; f < faces.size(); ++f)
    claim(faces[f].label, "a face", "layout.faces[" + std::to_string(f) + "].label");
}

// The output: empty, or a node that is a signal or the mixer part.
void check_output(Checker& c) {
  const std::string& out = c.patch.graph.output;
  if (out.empty()) return;
  const Node* n = c.node(out);
  if (!n) { c.add("graph.output", in_quotes(out) + " names no node"); return; }
  const TypeDeclarations* d = type_declarations(n->type);
  if (d && !d->signal && n->type != "StereoMixer")
    c.add("graph.output", in_quotes(out) + " is a " + n->type + ", a part, not a signal");
}

// Every name of every map declared for that map's kind.
void check_declared(Checker& c, const NodeAt& at) {
  const auto refuse = [&](const std::string& name, const char* kind) {
    c.add(at.param_path(name), at.opening + in_quotes(name) + " is not " + kind + " of " + at.node.type);
  };
  for (const auto& [name, v] : at.node.values)
    if (!pin_of(at.d, name).declared) refuse(name, "a pin");
  for (const auto& [name, feeds] : at.node.wires)
    if (!pin_of(at.d, name).declared) refuse(name, "a pin");
  for (const auto& [name, v] : at.node.settings) {
    const auto* s = at.d.setting(name);
    if (!s || s->type == SettingType::String) refuse(name, "a setting");
  }
  for (const auto& [name, v] : at.node.strings) {
    const auto* s = at.d.setting(name);
    if (!s || s->type != SettingType::String) refuse(name, "a string setting");
  }
  for (const auto& [name, v] : at.node.arrays)
    if (!at.d.array(name)) refuse(name, "an array");
}

// A pin holds a number or feeds; a feed list is never empty; one feed unless
// the pin is a list; a slot takes neither a number nor a tap.
void check_pin_shapes(Checker& c, const NodeAt& at) {
  const auto& n = at.node;
  for (const auto& [name, feeds] : n.wires)
    if (pin_of(at.d, name).declared && !feeds.empty() && n.values.count(name))
      c.add(at.param_path(name), at.opening + in_quotes(name) + " holds a number and a feed: a pin takes one");
  for (const auto& [name, feeds] : n.wires)
    if (pin_of(at.d, name).declared && feeds.empty())
      c.add(at.param_path(name), at.opening + in_quotes(name) +
                                 " holds an empty feed list: a pin with no feed has no entry");
  for (const auto& [name, feeds] : n.wires) {
    const Pin pin = pin_of(at.d, name);
    if (pin.declared && !pin.list && feeds.size() > 1)
      c.add(at.param_path(name), at.opening + in_quotes(name) + " takes one feed, and holds " +
                                 std::to_string(feeds.size()));
  }
  for (const auto& [name, v] : n.values)
    if (pin_of(at.d, name).slot)
      c.add(at.param_path(name), at.opening + in_quotes(name) + " is a slot: it takes a part, not a number");
  for (const auto& [name, feeds] : n.wires) {
    const bool tapped = std::any_of(feeds.begin(), feeds.end(), [](const Wire& w) { return w.tap; });
    if (pin_of(at.d, name).slot && tapped)
      c.add(at.param_path(name), at.opening + in_quotes(name) +
                                 " is a slot: it takes a part by ref, not through a tap");
  }
}

// The door's refusal of a feed, as the loader words it.
std::string refused_feed(const std::string& pin, const TypeDeclarations& source, const Pin& at) {
  std::string takes;
  if (at.slot && !at.slot->hint.empty()) takes = "; " + in_quotes(pin) + " takes " + at.slot->hint;
  return in_quotes(pin) + " does not accept a " + source.type +
         (source.signal ? "" : ", a part, not a signal") + takes;
}

// Each feed names a node; a slot's feed is of a type its door takes; a
// signal pin's feed is a signal.
void check_feeds(Checker& c, const NodeAt& at) {
  for (const auto& [name, feeds] : at.node.wires) {
    const Pin pin = pin_of(at.d, name);
    if (!pin.declared) continue;
    for (const auto& w : feeds) {
      const Node* source = c.node(w.source);
      if (!source) {
        c.add(at.param_path(name), at.opening + in_quotes(name) + " reads " + in_quotes(w.source) +
                                   ", which names no node");
        continue;
      }
      const TypeDeclarations* sd = type_declarations(source->type);
      if (!sd) continue;
      const bool accepted = pin.slot
          ? w.tap || std::find(pin.slot->accepts.begin(), pin.slot->accepts.end(), sd->type) !=
                         pin.slot->accepts.end()
          : sd->signal;
      if (!accepted) c.add(at.param_path(name), at.opening + refused_feed(name, *sd, pin));
    }
  }
}

// A scalar setting within its declaration: a bool 0 or 1, an enum an index
// into its labels, a float or an int inside its range, an int whole.
void check_settings(Checker& c, const NodeAt& at) {
  for (const auto& [name, v] : at.node.settings) {
    const auto* s = at.d.setting(name);
    if (!s || s->type == SettingType::String) continue;
    const std::string head = at.opening + in_quotes(name) + " is " + float_text(v);
    if (s->type == SettingType::Bool) {
      if (v != 0.0f && v != 1.0f) c.add(at.param_path(name), head + ": a bool setting holds 0 or 1");
      continue;
    }
    if (!s->labels.empty()) {
      if (!is_index(v, s->labels.size()))
        c.add(at.param_path(name), head + ", which is not an index into its labels (" + joined(s->labels) + ")");
      continue;
    }
    if (!(v >= s->min && v <= s->max))
      c.add(at.param_path(name), head + ", outside its range " + float_text(s->min) + " to " + float_text(s->max));
    if (s->type == SettingType::Int && std::isfinite(v) && v != std::trunc(v))
      c.add(at.param_path(name), head + ": an int setting holds a whole number");
  }
}

// A labelled column's every row an index into its labels; the table rule.
void check_arrays(Checker& c, const NodeAt& at) {
  for (const auto& [name, rows] : at.node.arrays) {
    const auto* a = at.d.array(name);
    if (!a || a->labels.empty()) continue;
    for (size_t i = 0; i < rows.size(); ++i)
      if (!is_index(rows[i], a->labels.size()))
        c.add(at.param_path(name), at.opening + in_quotes(name) + " row " + std::to_string(i) + " is " +
                                   float_text(rows[i]) + ", which is not an index into its labels (" +
                                   joined(a->labels) + ")");
  }
  const Component* proto = prototype(at.node.type);
  if (!proto) return;
  const std::string ragged = ragged_table_refusal(*proto, [&](const char* column) {
    auto it = at.node.arrays.find(column);
    return it == at.node.arrays.end() ? -1 : int(it->second.size());
  });
  if (!ragged.empty()) c.add(at.path + ".params", at.opening + ragged);
}

// Each dynamic pin on a declared setting that is not structural, read from a
// node that is a signal.
void check_dynamic_pins(Checker& c, const NodeAt& at) {
  for (const auto& [key, source] : at.node.dynamicPins) {
    const std::string path = at.path + ".dynamicPins." + key;
    const auto* s = at.d.setting(key);
    if (!s) {
      c.add(path, at.opening + in_quotes(key) + " is not a setting on " + at.node.type +
                  " (fixed pins belong in \"params\")");
      continue;
    }
    if (s->structural) {
      c.add(path, at.opening + in_quotes(key) + " is a structural setting on " + at.node.type +
                  " (it sizes or reshapes the node at prepare), so it takes no pin");
      continue;
    }
    const Node* n = c.node(source);
    if (!n) {
      c.add(path, at.opening + in_quotes(key) + " reads " + in_quotes(source) + ", which names no node");
      continue;
    }
    const TypeDeclarations* sd = type_declarations(n->type);
    if (sd && !sd->signal) c.add(path, at.opening + in_quotes(key) + " is a " + n->type + ", a part, not a signal");
  }
}

// One node: its type registered, then every rule that reads its declarations.
void check_node(Checker& c, size_t i) {
  const Node& n = c.patch.graph.nodes[i];
  const std::string path = node_path(i);
  const std::string opening = "node " + in_quotes(n.id) + " (" + n.type + "): ";
  const TypeDeclarations* d = type_declarations(n.type);
  if (!d) {
    std::string sentence = "Unknown node type: " + n.type;
    if (const char* hint = retired_type_hint(n.type)) sentence += std::string("; ") + hint;
    c.add(path + ".type", opening + sentence);
    return;
  }
  const NodeAt at{n, path, opening, *d};
  check_declared(c, at);
  check_pin_shapes(c, at);
  check_feeds(c, at);
  check_settings(c, at);
  check_arrays(c, at);
  check_dynamic_pins(c, at);
}

// The nodes each node reads over wires that are not taps and over its
// dynamic pins, each once, in the order the node names them.
std::vector<std::vector<size_t>> reads_of(const Checker& c) {
  const auto& nodes = c.patch.graph.nodes;
  std::vector<std::vector<size_t>> reads(nodes.size());
  const auto read = [&](size_t i, const std::string& id) {
    auto it = c.index.find(id);
    if (it != c.index.end() && std::find(reads[i].begin(), reads[i].end(), it->second) == reads[i].end())
      reads[i].push_back(it->second);
  };
  for (size_t i = 0; i < nodes.size(); ++i) {
    for (const auto& [pin, feeds] : nodes[i].wires)
      for (const auto& w : feeds)
        if (!w.tap) read(i, w.source);
    for (const auto& [setting, source] : nodes[i].dynamicPins) read(i, source);
  }
  return reads;
}

// A depth-first walk from each node in the graph's order; every read that
// returns to a node still on the walk's path is a cycle with no tap in it,
// named from that node along the path and back to it.
void check_cycles(Checker& c) {
  const auto reads = reads_of(c);
  const auto& nodes = c.patch.graph.nodes;
  std::vector<int> state(nodes.size(), 0);               // 0 unwalked, 1 on the path, 2 done
  std::vector<size_t> path;
  std::function<void(size_t)> walk = [&](size_t i) {
    state[i] = 1;
    path.push_back(i);
    for (size_t j : reads[i]) {
      if (state[j] == 0) { walk(j); continue; }
      if (state[j] != 1) continue;
      std::string cycle;
      for (auto k = std::find(path.begin(), path.end(), j); k != path.end(); ++k)
        cycle += nodes[*k].id + " -> ";
      c.add("graph", "a cycle with no tap in it: " + cycle + nodes[j].id);
    }
    path.pop_back();
    state[i] = 2;
  };
  for (size_t i = 0; i < nodes.size(); ++i)
    if (state[i] == 0) walk(i);
}

// A plain sound has no note: no PerformNode reads one, no dynamic pin is pushed.
void check_plain_sound(Checker& c) {
  if (c.patch.instrument) return;
  const auto& nodes = c.patch.graph.nodes;
  for (size_t i = 0; i < nodes.size(); ++i) {
    const std::string opening = "node " + in_quotes(nodes[i].id) + " (" + nodes[i].type + "): ";
    if (nodes[i].type == "PerformNode")
      c.add(node_path(i) + ".type", opening + "a plain sound has no note to read");
    for (const auto& [key, source] : nodes[i].dynamicPins)
      c.add(node_path(i) + ".dynamicPins." + key,
            opening + "a plain sound has no note, so nothing drives a dynamic pin");
  }
}

// Every position names a node; every group member a node, a group or a face;
// every face field a node.
void check_layout(Checker& c) {
  const Layout& l = c.patch.layout;
  for (const auto& [id, pos] : l.positions)
    if (!c.node(id)) c.add("layout.positions." + id, in_quotes(id) + " names no node");
  std::set<std::string, std::less<>> named;
  for (const auto& g : l.groups) named.insert(g.name);
  for (const auto& f : l.faces) named.insert(f.label);
  for (size_t g = 0; g < l.groups.size(); ++g)
    for (size_t m = 0; m < l.groups[g].members.size(); ++m) {
      const std::string& member = l.groups[g].members[m];
      if (!c.node(member) && !named.count(member))
        c.add("layout.groups[" + std::to_string(g) + "].members[" + std::to_string(m) + "]",
              in_quotes(member) + " names no node, group or face");
    }
  for (size_t f = 0; f < l.faces.size(); ++f)
    for (const auto& [field, id] : l.faces[f].fields)
      if (!c.node(id))
        c.add("layout.faces[" + std::to_string(f) + "].fields." + field, in_quotes(id) + " names no node");
}

} // namespace

std::vector<std::string> Patch::check() const {
  Checker c(*this);
  check_names(c);
  check_output(c);
  for (size_t i = 0; i < graph.nodes.size(); ++i) check_node(c, i);
  check_cycles(c);
  check_plain_sound(c);
  check_layout(c);
  return c.lines;
}

} // namespace mforce
