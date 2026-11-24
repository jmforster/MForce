// The edits that add, remove and rename a node, set its arrays, and set the
// instrument block.
#include "mforce/patch/edit.h"
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "mforce/patch/type_declarations.h"
#include "mforce/source/envelope/envelope.h"
#include "edit_support.h"
#include "float_text.h"
#include "node_complete.h"
#include "node_rules.h"
#include "sentence.h"

namespace mforce {

using namespace edit_detail;   // the shared helpers by their short names (C#: a `using` directive)

namespace {

using Columns = std::map<std::string, std::vector<float>>;

// An Envelope's stage columns as Envelope::make_adsr lays them out.
Columns adsr_stages(const TypeDeclarations& d) {
  const auto adsr = Envelope::make_adsr(0.05f, 0.1f, 0.7f, 0.2f);
  Columns columns;
  for (const auto& a : d.arrays)
    if (a.group == "stages") columns[a.name] = adsr->get_array(a.name);
  return columns;
}

// A CurveNode's two knots, (0,0) and (1,1): 0..1 mapped to itself.
Columns two_knots(const TypeDeclarations&) { return {{"x", {0.0f, 1.0f}}, {"y", {0.0f, 1.0f}}}; }

// The types a new node of which starts with arrays, and what it starts with.
// A pointer to a function is the C# `Func<TypeDeclarations, Columns>`.
struct StartingShape {
  const char* type;
  Columns (*columns)(const TypeDeclarations&);
};
constexpr StartingShape kStartingShapes[] = {
  {"Envelope", adsr_stages},
  {"CurveNode", two_knots},
};

// The types a new node's id is not the type's own name for.
struct ShownName {
  const char* type;
  const char* name;
};
constexpr ShownName kShownNames[] = {
  {"SoundChannel", "Channel"}, {"StereoMixer", "Mixer"}, {"PerformNode", "Note"}, {"CurveNode", "Curve"},
};

// The name a new node of `type` is given before a number makes it free.
std::string shown_name(std::string_view type) {
  for (const auto& s : kShownNames)
    if (type == s.type) return s.name;
  constexpr std::string_view kSuffix = "Source";
  if (type.size() > kSuffix.size() && type.ends_with(kSuffix)) type.remove_suffix(kSuffix.size());
  return std::string(type);
}

// Every place but the node itself that names `from` names `to` instead.
void rename_reads(Patch& p, const std::string& from, const std::string& to) {
  for (auto& n : p.graph.nodes) {
    for (auto& [pin, feeds] : n.wires)
      for (auto& w : feeds)
        if (w.source == from) w.source = to;
    for (auto& [setting, source] : n.dynamicPins)
      if (source == from) source = to;
  }
  if (p.graph.output == from) p.graph.output = to;
  if (auto it = p.layout.positions.find(from); it != p.layout.positions.end()) {
    const Vec2 pos = it->second;
    p.layout.positions.erase(it);
    p.layout.positions[to] = pos;
  }
  for (auto& g : p.layout.groups)
    for (auto& m : g.members)
      if (m == from) m = to;
  for (auto& f : p.layout.faces)
    for (auto& [field, id] : f.fields)
      if (id == from) id = to;
}

void require_finite_number(float v, const std::string& is, const char* what) {
  if (!std::isfinite(v)) throw std::runtime_error(is + float_text(v) + ": " + what + " is a finite number");
}

}  // namespace

Change add_node(Patch& p, std::string_view type, std::string_view id, std::uint32_t seed, Vec2 pos) {
  Node n;
  n.id = std::string(id);
  n.type = std::string(type);
  n.seed = seed;
  require_finite(pos, node_opening(n) + "its position");
  if (const TypeDeclarations* d = type_declarations(n.type)) {
    complete(n, *d);
    for (const auto& shape : kStartingShapes)
      if (n.type == shape.type) n.arrays = shape.columns(*d);
  }
  Patch copy = p;
  copy.layout.positions[n.id] = pos;
  copy.graph.nodes.push_back(std::move(n));
  return adopt(p, copy, Change::Structure);
}

std::string fresh_id(const Patch& p, std::string_view type) { return free_name(p, shown_name(type), true); }

std::string fresh_face_label(const Patch& p) { return free_name(p, "Note", false); }

Change remove_node(Patch& p, std::string_view node) {
  Patch copy = p;
  const auto index = index_of(copy, node);
  if (!index) throw std::runtime_error(in_quotes(node) + " names no node: there is nothing to remove");
  drop_unread_face_nodes(copy, detach_node(copy, *index));
  return adopt(p, copy, Change::Structure);
}

Change rename_node(Patch& p, std::string_view node, std::string_view to) {
  Patch copy = p;
  Node& n = node_named(copy, node, "there is nothing to rename");
  const std::string from = n.id, name(to);
  if (from == name) return Change::Structure;
  n.id = name;
  rename_reads(copy, from, name);
  return adopt(p, copy, Change::Structure);
}

Change set_arrays(Patch& p, std::string_view node, const std::map<std::string, std::vector<float>>& columns) {
  Patch copy = p;
  const NamedOnNode at = named_on_node(copy, node, columns.empty() ? std::string("arrays") : columns.begin()->first);
  const std::string opening = node_opening(at.node);
  for (const auto& [name, rows] : columns) {
    if (!at.d.array(name))
      throw std::runtime_error(opening + in_quotes(name) + " is not an array of " + at.node.type);
    for (std::size_t i = 0; i < rows.size(); ++i)
      require_finite_number(rows[i], opening + in_quotes(name) + " row " + std::to_string(i) + " is ",
                            "an array's row");
    at.node.arrays[name] = rows;
  }
  return adopt(p, copy, Change::Structure);
}

Change set_instrument(Patch& p, const InstrumentBlock& block) {
  require_finite_number(block.volume, "the instrument's volume is ", "a volume");
  require_finite_number(block.glideMs, "the instrument's glideMs is ", "a glide time");
  Patch copy = p;
  copy.instrument = block;
  return adopt(p, copy, Change::Structure);
}

} // namespace mforce
