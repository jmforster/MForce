#include "mforce/ui_views/canvas_table.h"
#include <algorithm>
#include <map>
#include <optional>
#include "mforce/patch/type_declarations.h"
#include "mforce/ui_model/groups.h"

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

// Where each giving end of the level is drawn: a node's Output and Tap
// pins, a shown face's field; where each taking end is; and a group's pins,
// by what they stand for.
struct Places {
  std::map<std::string, int> output, tap;          // node id -> pin integer
  std::map<std::string, int> taking;               // "node|name" -> pin integer, a setting's as "node|=name"
  std::map<std::string, std::pair<std::string, std::string>> faceOf;   // every face's node id -> (label, field)
  std::map<std::string, int> groupIn;              // in_key -> pin integer
  std::map<std::string, int> groupOut;             // out_key -> pin integer
  std::set<std::string> groups;                    // the groups shown
  int patchOutput{-1};                             // -1: the Output node is not shown (inside a group)
};

int pin_id(int node, int row) { return node * CanvasNames::kPinsPerNode + row; }

std::string in_key(const std::string& group, const std::string& node, const std::string& pin, std::size_t feed,
                   bool drive) {
  return group + "|" + node + "|" + pin + "|" + std::to_string(feed) + (drive ? "|=" : "|");
}

std::string out_key(const std::string& group, const std::string& node, bool tap) {
  return group + "|" + node + (tap ? "|tap" : "|out");
}

const Node* node_named(const Patch& p, const std::string& id) {
  for (const auto& n : p.graph.nodes)
    if (n.id == id) return &n;
  return nullptr;
}

CanvasNode graph_node(const Node& n, const Offers& offered, CanvasNames& names, Places& places) {
  CanvasNode c{names.node("n:" + n.id), CanvasNode::Kind::Graph, n.id, n.type, {}, 0, {}, {}};
  for (const auto& [name, text] : n.strings)
    if (!text.empty()) c.texts.push_back(text);
  const TypeDeclarations* d = type_declarations(n.type);
  int row = 2;
  const auto input = [&](const std::string& name, Look look, Kind kind) {
    const int id = pin_id(c.id, row++);
    places.taking[n.id + (kind == Kind::SettingPin ? "|=" : "|") + name] = id;
    const auto drive = kind == Kind::SettingPin ? n.dynamicPins.find(name) : n.dynamicPins.end();
    c.inputs.push_back(CanvasPin{id, PinEnd{kind, n.id, name}, look, name, false,
                                 drive == n.dynamicPins.end() ? std::string() : drive->second});
  };
  if (d) {
    for (const auto& p : d->params) input(p.name, Look::Param, Kind::Input);
    for (const auto& in : d->inputs) input(in.name, n.values.count(in.name) ? Look::Param : Look::InputOnly, Kind::Input);
    for (const auto& st : d->settings)
      if (offered.count({n.id, st.name}) || n.dynamicPins.count(st.name)) input(st.name, Look::Setting, Kind::SettingPin);
  }
  c.outputs.push_back(CanvasPin{pin_id(c.id, 0), PinEnd{Kind::Output, n.id, {}}, Look::Output, "out", false});
  places.output[n.id] = pin_id(c.id, 0);
  if (n.type == "Wormhole") {
    places.tap[n.id] = pin_id(c.id, 0);            // no tap pin: a file's tap of one leaves its one output
  } else if (!d || d->signal) {
    c.outputs.push_back(CanvasPin{pin_id(c.id, 1), PinEnd{Kind::Tap, n.id, {}}, Look::Tap, "tap", false});
    places.tap[n.id] = pin_id(c.id, 1);
  }
  return c;
}

CanvasNode face_node(const NoteFace& f, const std::vector<std::string>& fields, CanvasNames& names, Places& places) {
  CanvasNode c{names.node("f:" + f.label), CanvasNode::Kind::Face, f.label, {}, {}, 0, {}, {}};
  for (size_t k = 0; k < fields.size(); ++k) {
    const int id = pin_id(c.id, int(k));
    c.outputs.push_back(CanvasPin{id, PinEnd{Kind::NoteField, f.label, fields[k]}, Look::Field, fields[k], false});
    if (auto named = f.fields.find(fields[k]); named != f.fields.end()) {
      places.output[named->second] = id;
      places.tap[named->second] = id;
    }
  }
  return c;
}

// The pin a group gives for a member read from outside, and its label.
CanvasPin leaving_pin(int id, const Leaving& l, bool only, const Places& places) {
  if (const auto face = places.faceOf.find(l.node); face != places.faceOf.end())
    return CanvasPin{id, PinEnd{Kind::NoteField, face->second.first, face->second.second}, Look::Field,
                     face->second.first + "." + face->second.second, false, {}};
  const std::string label = only && !l.tap ? std::string("out") : l.node + (l.tap ? ".tap" : "");
  return CanvasPin{id, PinEnd{l.tap ? Kind::Tap : Kind::Output, l.node, {}}, l.tap ? Look::Tap : Look::Output, label,
                   false, {}};
}

CanvasNode group_node(const Patch& p, const std::string& name, CanvasNames& names, Places& places) {
  CanvasNode c{names.node("g:" + name), CanvasNode::Kind::Group, name, {}, {}, 0, {}, {}};
  std::set<std::string> faces;
  for (const auto& id : nodes_under(p, name)) {
    if (const auto face = places.faceOf.find(id); face != places.faceOf.end()) faces.insert(face->second.first);
    else ++c.under;
  }
  c.under += int(faces.size());
  const Boundary b = boundary(p, name);
  int row = 0;
  for (const auto& l : b.out) {
    c.outputs.push_back(leaving_pin(pin_id(c.id, row), l, b.out.size() == 1, places));
    places.groupOut[out_key(name, l.node, l.tap)] = pin_id(c.id, row++);
  }
  for (const auto& in : b.in) {
    const int id = pin_id(c.id, row++);
    places.groupIn[in_key(name, in.node, in.pin, in.feed, in.drive)] = id;
    c.inputs.push_back(CanvasPin{id, PinEnd{in.drive ? Kind::SettingPin : Kind::Input, in.node, in.pin},
                                 in.drive ? Look::Setting : Look::Param, in.node + "." + in.pin, false, {}});
  }
  places.groups.insert(name);
  return c;
}

CanvasNode output_node(const Patch& p, Places& places) {
  CanvasNode c{CanvasNames::kOutputNode, CanvasNode::Kind::Output, {}, {}, {}, 0, {}, {}};
  places.patchOutput = pin_id(c.id, 1);
  c.inputs.push_back(CanvasPin{places.patchOutput, PinEnd{Kind::PatchOutput, {}, {}}, Look::PatchOutput,
                               p.graph.output.empty() ? std::string("(nothing)") : p.graph.output, false, {}});
  return c;
}

// The level a table is made for, and what its nodes placed.
struct Level {
  const Patch& patch;
  const std::vector<std::string>& path;
  const Places& places;
  // The shown group standing for `id`; empty when what stands for it is not a group.
  std::string group_for(const std::string& id) const {
    const std::string rep = shown_for(patch, path, id);
    return places.groups.count(rep) ? rep : std::string();
  }
};

// The giving pin a feed from `source` starts at, and the group it leaves (empty: none).
std::optional<int> giving(const Level& at, const std::string& source, bool tap, std::string& group) {
  const auto& from = tap ? at.places.tap : at.places.output;
  if (const auto it = from.find(source); it != from.end()) return it->second;
  group = at.group_for(source);
  const auto out = at.places.groupOut.find(out_key(group, source, tap && !at.places.faceOf.count(source)));
  return out == at.places.groupOut.end() ? std::nullopt : std::optional<int>(out->second);
}

// The taking pin a feed into `reader`'s `name` ends at, and the group it enters.
std::optional<int> taking(const Level& at, const std::string& reader, const std::string& name, std::size_t feed,
                          bool drive, std::string& group) {
  if (const auto it = at.places.taking.find(reader + (drive ? "|=" : "|") + name); it != at.places.taking.end())
    return it->second;
  group = at.group_for(reader);
  const auto in = at.places.groupIn.find(in_key(group, reader, name, feed, drive));
  return in == at.places.groupIn.end() ? std::nullopt : std::optional<int>(in->second);
}

void add_link(CanvasTable& t, CanvasNames& names, std::optional<int> from, std::optional<int> to, const PinEnd& reader,
              std::size_t feed, CanvasLink::Look look) {
  if (!from || !to) return;
  const std::string key = std::to_string(int(reader.kind)) + "|" + reader.node + "|" + reader.name + "|" + std::to_string(feed);
  t.links.push_back(CanvasLink{names.link(key), *from, *to, reader, feed, look});
}

void mark_one_fed(CanvasTable& t, int id) {
  for (auto& n : t.nodes)
    for (auto& pin : n.inputs)
      if (pin.id == id) pin.fed = true;
}

// The pin `id` of the table is fed by `w` from outside the level: it is
// fed, and names the source.
void note_outside(CanvasTable& t, int id, const Wire& w) {
  for (auto& n : t.nodes)
    for (auto& pin : n.inputs)
      if (pin.id == id) {
        pin.outside += (pin.outside.empty() ? "" : ", ") + w.source + (w.tap ? " (tap)" : "");
        pin.fed = true;
      }
}

// One feed or drive of `n`, drawn when both its ends stand for something
// here and not both for one group; when only its taking end does, that pin
// names the source instead.
void link_one(CanvasTable& t, CanvasNames& names, const Level& at, const Node& n, const std::string& name,
              std::size_t feed, const Wire& w, bool drive) {
  const Node* source = node_named(at.patch, w.source);
  std::string fromGroup, toGroup;
  if (n.type == "Wormhole" && source && source->type == "Wormhole") {   // the span is never drawn; its pin is fed
    if (const auto to = taking(at, n.id, name, feed, drive, toGroup)) mark_one_fed(t, *to);
    return;
  }
  const auto from = giving(at, w.source, w.tap, fromGroup);
  const auto to = taking(at, n.id, name, feed, drive, toGroup);
  if (!fromGroup.empty() && fromGroup == toGroup) return;
  if (!from && to) note_outside(t, *to, w);
  const auto look = drive ? CanvasLink::Look::Dynamic : w.tap ? CanvasLink::Look::Tap : CanvasLink::Look::Plain;
  add_link(t, names, from, to, PinEnd{drive ? Kind::SettingPin : Kind::Input, n.id, name}, feed, look);
}

void add_links(CanvasTable& t, const Level& at, CanvasNames& names) {
  for (const Node& n : at.patch.graph.nodes) {
    if (at.places.faceOf.count(n.id)) continue;            // a face's node reads nothing
    for (const auto& [pin, feeds] : n.wires)
      for (std::size_t f = 0; f < feeds.size(); ++f) link_one(t, names, at, n, pin, f, feeds[f], false);
    for (const auto& [setting, source] : n.dynamicPins) link_one(t, names, at, n, setting, 0, Wire{source, false}, true);
  }
  if (at.patch.graph.output.empty() || at.places.patchOutput < 0) return;
  std::string group;
  add_link(t, names, giving(at, at.patch.graph.output, false, group), at.places.patchOutput,
           PinEnd{Kind::PatchOutput, {}, {}}, 0, CanvasLink::Look::Plain);
}

// Each pin a link reaches is marked fed too (one fed from outside the level,
// or by a Wormhole's span, is already).
void mark_fed(CanvasTable& t) {
  std::set<int> reached;
  for (const auto& l : t.links) {
    reached.insert(l.from);
    reached.insert(l.to);
  }
  for (auto& n : t.nodes) {
    for (auto& pin : n.inputs) pin.fed = pin.fed || reached.count(pin.id) != 0;
    for (auto& pin : n.outputs) pin.fed = pin.fed || reached.count(pin.id) != 0;
  }
}

}  // namespace

std::string pin_tooltip(const CanvasPin& pin) {
  if (pin.look != Look::Setting) return {};
  if (!pin.driver.empty())
    return "Dynamic pin: '" + pin.label + "' set once at note-on by '" + pin.driver + "',\n"
           "frozen for the life of the note.\n"
           "Drag a node's output here to change the driver;\n"
           "delete the wire to unwire, withdraw the pin with the panel's mark.";
  return "Dynamic pin (unwired): '" + pin.label + "'. Drag a node's output\n"
         "here. Until wired, the panel's value applies.\n"
         "Withdraw the pin with the panel's mark.";
}

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

CanvasTable canvas_table(const Patch& patch, const std::vector<std::string>& path, const Offers& offered,
                         CanvasNames& names) {
  CanvasTable t;
  Places places;
  const std::vector<std::string> fields = note_fields();
  for (const auto& f : patch.layout.faces)
    for (const auto& [field, id] : f.fields) places.faceOf[id] = {f.label, field};
  for (const auto& name : shown_at(patch, path)) {
    if (const Node* n = node_named(patch, name)) {
      t.nodes.push_back(graph_node(*n, offered, names, places));
      continue;
    }
    bool face = false;
    for (const auto& f : patch.layout.faces)
      if (f.label == name) {
        t.nodes.push_back(face_node(f, fields, names, places));
        face = true;
      }
    if (!face) t.nodes.push_back(group_node(patch, name, names, places));
  }
  if (path.empty()) t.nodes.push_back(output_node(patch, places));
  add_links(t, Level{patch, path, places}, names);
  mark_fed(t);
  return t;
}

}  // namespace mforce::ui
