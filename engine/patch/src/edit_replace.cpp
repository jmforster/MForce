// One node made a node of another type, carrying by name what the new type
// takes and naming what it does not (replace_node).
#include "mforce/patch/edit.h"
#include <algorithm>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include "mforce/patch/type_declarations.h"
#include "edit_support.h"
#include "node_rules.h"
#include "sentence.h"

namespace mforce {

using namespace edit_detail;   // the shared helpers by their short names (C#: a `using` directive)

namespace {

using Decl = TypeDeclarations;

// The replacement under way: the node made, the node it replaces, both
// types' declarations (the old one's null for a type the registry lacks),
// and what has been carried and lost so far.
struct Carry {
  Node& made;
  const Node& old;
  const Decl* od;
  const Decl& nd;
  Replaced& r;
  void drop(const std::string& name) {
    if (std::find(r.dropped.begin(), r.dropped.end(), name) == r.dropped.end()) r.dropped.push_back(name);
  }
};

// Whether `pin` takes a feed from a node of declarations `source` (null: a
// type the registry lacks, which check() does not judge either).
bool takes(const Pin& pin, const Decl* source, bool tap) {
  if (!pin.declared) return false;
  if (!source) return true;
  if (pin.slot) return tap || std::find(pin.slot->accepts.begin(), pin.slot->accepts.end(), source->type) !=
                                  pin.slot->accepts.end();
  return source->signal;
}

const Decl* decl_of(const Patch& p, const Carry& c, const std::string& id) {
  if (id == c.old.id) return &c.nd;
  const auto index = index_of(p, id);
  return index ? type_declarations(p.graph.nodes[*index].type) : nullptr;
}

void carry_values(Carry& c) {
  for (const auto& [name, v] : c.old.values) {
    const Pin pin = pin_of(c.nd, name);
    if (pin.declared && !pin.slot) c.made.values[name] = v;
    else if (const auto* param = c.od ? c.od->param(name) : nullptr; !param || v != param->defaultValue) c.drop(name);
  }
}

// The value `v` of the old setting `os` (null when the old type declares
// none) as the new setting `ns` holds it; none when it has no place there.
std::optional<float> setting_value(const Decl::Setting* os, const Decl::Setting& ns, float v) {
  const bool oldEnum = os && !os->labels.empty();
  if (oldEnum != !ns.labels.empty()) return std::nullopt;
  if (!oldEnum) return setting_faults(ns, v).empty() ? std::optional<float>(v) : std::nullopt;
  if (!is_index(v, os->labels.size())) return std::nullopt;
  const auto it = std::find(ns.labels.begin(), ns.labels.end(), os->labels[std::size_t(v)]);
  return it == ns.labels.end() ? std::nullopt : std::optional<float>(float(it - ns.labels.begin()));
}

void carry_settings(Carry& c) {
  for (const auto& [name, v] : c.old.settings) {
    const auto* os = c.od ? scalar_setting(*c.od, name) : nullptr;
    const auto* ns = scalar_setting(c.nd, name);
    const std::optional<float> to = ns ? setting_value(os, *ns, v) : std::nullopt;
    if (to) c.made.settings[name] = *to;
    else if (!os || v != os->defaultValue) c.drop(name);
  }
  for (const auto& [name, text] : c.old.strings) {
    const auto* os = c.od ? string_setting(*c.od, name) : nullptr;
    if (string_setting(c.nd, name)) c.made.strings[name] = text;
    else if (!os || text != os->defaultText) c.drop(name);
  }
}

// The arrays go across whole when the new type declares exactly the columns
// the node holds, each in the same table and with the same labels.
void carry_arrays(Carry& c) {
  if (c.old.arrays.empty()) return;
  bool same = c.nd.arrays.size() == c.old.arrays.size();
  for (const auto& [name, rows] : c.old.arrays) {
    const auto* na = c.nd.array(name);
    const auto* oa = c.od ? c.od->array(name) : nullptr;
    same = same && na && oa && na->group == oa->group && na->labels == oa->labels;
  }
  if (same) c.made.arrays = c.old.arrays;
  else c.drop("arrays");
}

void carry_drives(Carry& c) {
  for (const auto& [setting, source] : c.old.dynamicPins) {
    const auto* s = c.nd.setting(setting);
    if (s && !s->structural) c.made.dynamicPins[setting] = source == c.old.id ? c.made.id : source;
    else c.drop(setting);
  }
}

// Each wire into the old node, onto the new node's pin of the same name when
// that pin takes its source (one feed on a pin that takes one).
void carry_wires_in(const Patch& p, Carry& c) {
  for (const auto& [name, feeds] : c.old.wires) {
    const Pin pin = pin_of(c.nd, name);
    std::vector<Wire> kept;
    for (const auto& w : feeds) {
      const bool fits = (pin.list || kept.empty()) && takes(pin, decl_of(p, c, w.source), w.tap);
      if (!fits) {
        c.drop(name);
        continue;
      }
      kept.push_back(Wire{w.source == c.old.id ? c.made.id : w.source, w.tap});
      ++c.r.wiresKept;
    }
    if (kept.empty()) continue;
    c.made.values.erase(name);
    c.made.wires[name] = std::move(kept);
  }
}

// Each feed and drive of another node that read the old node, and the
// patch's output: led to the new node when the reader takes it, else gone.
void carry_wires_out(Patch& p, Carry& c) {
  for (auto& n : p.graph.nodes) {
    if (&n == &c.made) continue;
    const Decl* rd = type_declarations(n.type);
    std::vector<std::string> emptied;
    for (auto& [name, feeds] : n.wires) {
      const Pin pin = rd ? pin_of(*rd, name) : Pin{true, true, nullptr};
      std::vector<Wire> kept;
      for (const auto& w : feeds) {
        if (w.source != c.old.id) {
          kept.push_back(w);
        } else if (takes(pin, &c.nd, w.tap)) {
          kept.push_back(Wire{c.made.id, w.tap});
          ++c.r.wiresKept;
        } else {
          c.drop(n.id + "." + name);
        }
      }
      feeds = std::move(kept);
      if (feeds.empty()) emptied.push_back(name);
    }
    for (const auto& name : emptied) pin_lost_its_feeds(n, name);
    std::map<std::string, std::string> drives;
    for (const auto& [setting, source] : n.dynamicPins) {
      if (source != c.old.id) {
        drives[setting] = source;
      } else if (c.nd.signal) {
        drives[setting] = c.made.id;
        ++c.r.wiresKept;
      } else {
        c.drop(n.id + "." + setting);
      }
    }
    n.dynamicPins = std::move(drives);
  }
  if (p.graph.output != c.old.id) return;
  if (c.nd.signal || c.nd.type == "StereoMixer") {
    p.graph.output = c.made.id;
  } else {
    p.graph.output.clear();
    c.drop("output");
  }
}

// A node of `type` under `id` as add_node makes one, with `old`'s seed.
Node new_node(std::string_view type, const std::string& id, const Node& old) {
  Patch scratch;
  scratch.instrument = InstrumentBlock{};
  (void)add_node(scratch, type, id, old.seed.value_or(0), Vec2{});
  Node made = scratch.graph.nodes.front();
  made.seed = old.seed;
  made.comment = old.comment;
  return made;
}

}  // namespace

Replaced replace_node(Patch& p, std::string_view node, std::string_view type) {
  Patch copy = p;
  const auto index = index_of(copy, node);
  if (!index) throw std::runtime_error(in_quotes(node) + " names no node: there is nothing to replace");
  for (const auto& f : copy.layout.faces)
    for (const auto& [field, id] : f.fields)
      if (id == node)
        throw std::runtime_error(in_quotes(node) + " is a node of note face " + in_quotes(f.label) +
                                 ": it is not replaced");
  const Node old = copy.graph.nodes[*index];
  Replaced r;
  r.id = fresh_id(copy, type);
  const Decl* nd = type_declarations(type);
  if (!nd) throw std::runtime_error("node " + in_quotes(r.id) + " (" + std::string(type) + "): " +
                                    unknown_type_sentence(std::string(type)));
  copy.graph.nodes[*index] = new_node(type, r.id, old);
  Carry c{copy.graph.nodes[*index], old, type_declarations(old.type), *nd, r};
  carry_values(c);
  carry_settings(c);
  carry_arrays(c);
  carry_drives(c);
  carry_wires_in(copy, c);
  carry_wires_out(copy, c);
  if (const auto pos = copy.layout.positions.find(old.id); pos != copy.layout.positions.end()) {
    const Vec2 at = pos->second;
    copy.layout.positions.erase(pos);
    copy.layout.positions[r.id] = at;
  }
  if (Group* holder = group_holding(copy, old.id)) std::replace(holder->members.begin(), holder->members.end(), old.id, r.id);
  (void)adopt(p, copy, Change::Structure);
  return r;
}

} // namespace mforce
