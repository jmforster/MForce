#pragma once
// The small patch the placement tests lay out, and what they measure it with.
#include "mforce/patch/patch.h"
#include "mforce/patch/type_declarations.h"
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace placement_fixture {

using namespace mforce;

inline Node node(const char* id, const char* type) {
  Node n;
  n.id = id;
  n.type = type;
  return n;
}

// osc is the output and reads pf and env; env reads osc through a tap (a
// loop) and has a dynamic pin from slow; lone is read by nothing.
inline Patch small() {
  Patch p;
  Node osc = node("osc", "SineSource");
  osc.wires["frequency"] = {Wire{"pf"}};
  osc.wires["amplitude"] = {Wire{"env"}};
  Node env = node("env", "Envelope");
  env.wires["minValue"] = {Wire{"osc", true}};
  env.dynamicPins["timeScale"] = "slow";
  Node pf = node("pf", "PerformNode");
  pf.settings["field"] = 0.0f;
  p.graph.nodes = {node("lone", "SineSource"), pf, env, node("slow", "SineSource"), osc};
  p.graph.output = "osc";
  p.instrument = InstrumentBlock{};
  return p;
}

inline Vec2 at(const Patch& p, const char* id) { return p.layout.positions.at(id); }

// No two things (nodes, faces) at one point.
inline bool all_distinct(const Patch& p) {
  std::set<std::pair<float, float>> seen;
  for (const auto& [id, pos] : p.layout.positions)
    if (!seen.insert({pos.x, pos.y}).second) return false;
  for (const auto& f : p.layout.faces)
    if (f.pos && !seen.insert({f.pos->x, f.pos->y}).second) return false;
  return true;
}

inline std::set<std::string> face_nodes(const Patch& p) {
  std::set<std::string> ids;
  for (const auto& f : p.layout.faces)
    for (const auto& [field, id] : f.fields) ids.insert(id);
  return ids;
}

// The height placement gives a node's slot, or a face's (null: six rows).
inline float slot_of(const Node* n) {
  float rows = 6.0f;
  if (n) {
    const TypeDeclarations* d = type_declarations(n->type);
    rows = float((d ? d->params.size() + d->inputs.size() : 0) + n->dynamicPins.size() + 1);
  }
  return 40.0f + 22.0f * rows + 20.0f;
}

// Whether two things `after` placed, that `before` had no position for, sit
// in one column with their slots overlapping.
inline bool new_slots_overlap(const Patch& before, const Patch& after) {
  std::vector<std::pair<Vec2, float>> slots;
  for (const auto& n : after.graph.nodes)
    if (!before.layout.positions.count(n.id) && after.layout.positions.count(n.id))
      slots.push_back({after.layout.positions.at(n.id), slot_of(&n)});
  for (size_t f = 0; f < after.layout.faces.size() && f < before.layout.faces.size(); ++f)
    if (!before.layout.faces[f].pos && after.layout.faces[f].pos)
      slots.push_back({*after.layout.faces[f].pos, slot_of(nullptr)});
  for (size_t i = 0; i < slots.size(); ++i)
    for (size_t j = i + 1; j < slots.size(); ++j) {
      const Vec2 a = slots[i].first, b = slots[j].first;
      if (a.x == b.x && a.y < b.y + slots[j].second && b.y < a.y + slots[i].second) return true;
    }
  return false;
}

}  // namespace placement_fixture
