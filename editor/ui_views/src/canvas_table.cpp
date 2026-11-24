#include "mforce/ui_views/canvas_table.h"
#include <algorithm>
#include <map>
#include <optional>
#include "mforce/patch/type_declarations.h"

namespace mforce::ui {

namespace {

using Kind = PinEnd::Kind;
using Look = CanvasPin::Look;
using Offers = std::set<std::pair<std::string, std::string>>;

// The note's fields, as PerformNode labels its `field` setting.
std::vector<std::string> note_fields() {
  const TypeDeclarations* d = type_declarations("PerformNode");
  const auto* field = d ? d->setting("field") : nullptr;
  return field ? field->labels : std::vector<std::string>{};
}

// Where each giving end of the patch is drawn: a node's Output and Tap pins,
// a face node's field; and where each taking end is.
struct Places {
  std::map<std::string, int> output, tap;          // node id -> pin integer
  std::map<std::string, int> taking;               // "node|name" -> pin integer, a setting's as "node|=name"
  std::map<std::string, std::pair<std::string, std::string>> faceOf;   // a face's node id -> (label, field)
  int patchOutput{0};
};

int pin_id(int node, int row) { return node * CanvasNames::kPinsPerNode + row; }

CanvasNode graph_node(const Node& n, const Offers& offered, CanvasNames& names, Places& places) {
  CanvasNode c{names.node("n:" + n.id), CanvasNode::Kind::Graph, n.id, n.type, {}, {}, {}};
  for (const auto& [name, text] : n.strings)
    if (!text.empty()) c.texts.push_back(text);
  const TypeDeclarations* d = type_declarations(n.type);
  int row = 2;
  const auto input = [&](const std::string& name, Look look, Kind kind) {
    const int id = pin_id(c.id, row++);
    places.taking[n.id + (kind == Kind::SettingPin ? "|=" : "|") + name] = id;
    c.inputs.push_back(CanvasPin{id, PinEnd{kind, n.id, name}, look, name, false});
  };
  if (d) {
    for (const auto& p : d->params) input(p.name, Look::Param, Kind::Input);
    for (const auto& in : d->inputs) input(in.name, n.values.count(in.name) ? Look::Param : Look::InputOnly, Kind::Input);
    for (const auto& st : d->settings)
      if (offered.count({n.id, st.name}) || n.dynamicPins.count(st.name)) input(st.name, Look::Setting, Kind::SettingPin);
  }
  c.outputs.push_back(CanvasPin{pin_id(c.id, 0), PinEnd{Kind::Output, n.id, {}}, Look::Output, "out", false});
  places.output[n.id] = pin_id(c.id, 0);
  if (!d || d->signal) {
    c.outputs.push_back(CanvasPin{pin_id(c.id, 1), PinEnd{Kind::Tap, n.id, {}}, Look::Tap, "tap", false});
    places.tap[n.id] = pin_id(c.id, 1);
  }
  return c;
}

CanvasNode face_node(const NoteFace& f, const std::vector<std::string>& fields, CanvasNames& names, Places& places) {
  CanvasNode c{names.node("f:" + f.label), CanvasNode::Kind::Face, f.label, {}, {}, {}, {}};
  for (size_t k = 0; k < fields.size(); ++k) {
    const int id = pin_id(c.id, int(k));
    c.outputs.push_back(CanvasPin{id, PinEnd{Kind::NoteField, f.label, fields[k]}, Look::Field, fields[k], false});
    if (auto named = f.fields.find(fields[k]); named != f.fields.end()) {
      places.output[named->second] = id;
      places.tap[named->second] = id;
      places.faceOf[named->second] = {f.label, fields[k]};
    }
  }
  return c;
}

CanvasNode output_node(const Patch& p, Places& places) {
  CanvasNode c{CanvasNames::kOutputNode, CanvasNode::Kind::Output, {}, {}, {}, {}, {}};
  places.patchOutput = pin_id(c.id, 1);
  c.inputs.push_back(CanvasPin{places.patchOutput, PinEnd{Kind::PatchOutput, {}, {}}, Look::PatchOutput,
                               p.graph.output.empty() ? std::string("(nothing)") : p.graph.output, false});
  return c;
}

// The giving pin a feed from `source` starts at; none when it is not drawn.
std::optional<int> giving(const Places& places, const std::string& source, bool tap) {
  const auto& from = tap ? places.tap : places.output;
  const auto it = from.find(source);
  return it == from.end() ? std::nullopt : std::optional<int>(it->second);
}

void add_link(CanvasTable& t, CanvasNames& names, std::optional<int> from, int to, const PinEnd& reader,
              std::size_t feed, CanvasLink::Look look) {
  if (!from) return;
  const std::string key = std::to_string(int(reader.kind)) + "|" + reader.node + "|" + reader.name + "|" + std::to_string(feed);
  t.links.push_back(CanvasLink{names.link(key), *from, to, reader, feed, look});
}

void add_links(CanvasTable& t, const Patch& p, CanvasNames& names, const Places& places) {
  for (const Node& n : p.graph.nodes) {
    if (places.faceOf.count(n.id)) continue;               // a face's node is not drawn
    for (const auto& [pin, feeds] : n.wires) {
      const auto to = places.taking.find(n.id + "|" + pin);
      if (to == places.taking.end()) continue;
      for (std::size_t f = 0; f < feeds.size(); ++f)
        add_link(t, names, giving(places, feeds[f].source, feeds[f].tap), to->second, PinEnd{Kind::Input, n.id, pin}, f,
                 feeds[f].tap ? CanvasLink::Look::Tap : CanvasLink::Look::Plain);
    }
    for (const auto& [setting, source] : n.dynamicPins) {
      const auto to = places.taking.find(n.id + "|=" + setting);
      if (to != places.taking.end())
        add_link(t, names, giving(places, source, false), to->second, PinEnd{Kind::SettingPin, n.id, setting}, 0,
                 CanvasLink::Look::Dynamic);
    }
  }
  if (!p.graph.output.empty())
    add_link(t, names, giving(places, p.graph.output, false), places.patchOutput, PinEnd{Kind::PatchOutput, {}, {}}, 0,
             CanvasLink::Look::Plain);
}

// Each pin a link reaches is marked fed.
void mark_fed(CanvasTable& t) {
  std::set<int> reached;
  for (const auto& l : t.links) {
    reached.insert(l.from);
    reached.insert(l.to);
  }
  for (auto& n : t.nodes) {
    for (auto& pin : n.inputs) pin.fed = reached.count(pin.id) != 0;
    for (auto& pin : n.outputs) pin.fed = reached.count(pin.id) != 0;
  }
}

}  // namespace

int CanvasNames::node(const std::string& key) {
  const auto [it, fresh] = nodes_.try_emplace(key, nextNode_);
  if (fresh) ++nextNode_;
  return it->second;
}

int CanvasNames::link(const std::string& key) {
  const auto [it, fresh] = links_.try_emplace(key, nextLink_);
  if (fresh) ++nextLink_;
  return it->second;
}

const PinEnd* CanvasTable::pin(int id) const {
  for (const auto& n : nodes) {
    for (const auto& p : n.inputs)
      if (p.id == id) return &p.end;
    for (const auto& p : n.outputs)
      if (p.id == id) return &p.end;
  }
  return nullptr;
}

const CanvasLink* CanvasTable::link(int id) const {
  const auto it = std::find_if(links.begin(), links.end(), [&](const CanvasLink& l) { return l.id == id; });
  return it == links.end() ? nullptr : &*it;
}

const CanvasNode* CanvasTable::node(int id) const {
  const auto it = std::find_if(nodes.begin(), nodes.end(), [&](const CanvasNode& n) { return n.id == id; });
  return it == nodes.end() ? nullptr : &*it;
}

CanvasTable canvas_table(const Patch& patch, const Offers& offered, CanvasNames& names) {
  CanvasTable t;
  Places places;
  const std::vector<std::string> fields = note_fields();
  std::vector<CanvasNode> faces;
  for (const auto& f : patch.layout.faces) faces.push_back(face_node(f, fields, names, places));
  std::set<std::string> seen;
  for (const Node& n : patch.graph.nodes)
    if (!places.faceOf.count(n.id) && seen.insert(n.id).second) t.nodes.push_back(graph_node(n, offered, names, places));
  for (auto& f : faces) t.nodes.push_back(std::move(f));
  t.nodes.push_back(output_node(patch, places));
  add_links(t, patch, names, places);
  mark_fed(t);
  return t;
}

}  // namespace mforce::ui
