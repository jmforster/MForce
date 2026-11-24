// Which strips there are, and the pop-outs.
#include "mforce/ui_control/waveforms.h"
#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "mforce/core/source_registry.h"
#include "mforce/patch/type_declarations.h"
#include "mforce/ui_control/session.h"

namespace mforce::ui {

namespace {

// A node of the Envelope family. type_declarations first: it fills the registry.
bool is_envelope(const std::string& type) {
  if (!type_declarations(type)) return false;
  const SourceRegistry& reg = SourceRegistry::instance();
  return reg.has(type) && reg.family(type) == Family::Envelope;
}

// The nodes a Note reads its fields through: every PerformNode, and every
// node a face names.
std::set<std::string> note_nodes(const Patch& p) {
  std::set<std::string> ids;
  for (const auto& n : p.graph.nodes)
    if (n.type == "PerformNode") ids.insert(n.id);
  for (const auto& f : p.layout.faces)
    for (const auto& [field, id] : f.fields) ids.insert(id);
  return ids;
}

// Each captured node's samples, by id.
std::map<std::string, std::span<const float>> captured(const Take& t) {
  std::map<std::string, std::span<const float>> by;
  for (std::size_t i = 0; i < t.stripIds.size() && i < t.strips.size(); ++i) by[t.stripIds[i]] = t.strips[i];
  return by;
}

Strip output_strip(const Session& s) { return Strip{"Output", "", false, std::span<const float>(s.take.mono)}; }

}  // namespace

std::vector<Strip> strips_of(const Session& s) {
  std::vector<Strip> strips{output_strip(s)};
  const Patch& p = s.open.patch;
  const auto notes = note_nodes(p);
  const auto samples = captured(s.take);
  for (const auto& n : p.graph.nodes) {
    const auto at = samples.find(n.id);
    if (at == samples.end() || notes.count(n.id)) continue;
    const bool envelope = is_envelope(n.type);
    if (envelope && !s.waves.envelopes) continue;
    strips.push_back(Strip{n.id, n.id, envelope, at->second});
  }
  return strips;
}

Strip strip_of(const Session& s, const std::string& node) {
  if (node.empty()) return output_strip(s);
  Strip strip{node, node, false, {}};
  const auto& nodes = s.open.patch.graph.nodes;
  const auto n = std::find_if(nodes.begin(), nodes.end(), [&](const Node& x) { return x.id == node; });
  if (n == nodes.end()) return strip;
  strip.envelope = is_envelope(n->type);
  const auto samples = captured(s.take);
  if (const auto at = samples.find(node); at != samples.end()) strip.samples = at->second;
  return strip;
}

void open_pop_out(Session& s, const std::string& node) {
  s.popOuts.push_back(PopOut{s.nextPopOut++, node, s.waves});
}

void close_pop_out(Session& s, int number) {
  std::erase_if(s.popOuts, [number](const PopOut& p) { return p.number == number; });
}

void set_over_strips(Session& s, bool over) { s.overStrips = over; }

}  // namespace mforce::ui
