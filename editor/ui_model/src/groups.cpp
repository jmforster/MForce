#include "mforce/ui_model/groups.h"
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include "mforce/patch/type_declarations.h"

namespace mforce::ui {

namespace {

using Names = std::set<std::string, std::less<>>;

const Group* group_named(const Patch& p, std::string_view name) {
  for (const auto& g : p.layout.groups)
    if (g.name == name) return &g;
  return nullptr;
}

const NoteFace* face_named(const Patch& p, std::string_view label) {
  for (const auto& f : p.layout.faces)
    if (f.label == label) return &f;
  return nullptr;
}

const Node* node_named(const Patch& p, std::string_view id) {
  for (const auto& n : p.graph.nodes)
    if (n.id == id) return &n;
  return nullptr;
}

Names face_nodes(const Patch& p) {
  Names ids;
  for (const auto& f : p.layout.faces)
    for (const auto& [field, id] : f.fields) ids.insert(id);
  return ids;
}

// The graph nodes `member` stands for: a node, a face's nodes, or what a
// group holds, through nested groups (each group once).
void collect(const Patch& p, const std::string& member, Names& into, Names& groupsSeen) {
  if (const Group* g = group_named(p, member)) {
    if (!groupsSeen.insert(member).second) return;
    for (const auto& m : g->members) collect(p, m, into, groupsSeen);
  } else if (const NoteFace* f = face_named(p, member)) {
    for (const auto& [field, id] : f->fields) into.insert(id);
  } else if (node_named(p, member)) {
    into.insert(member);
  }
}

Names under_all(const Patch& p, const std::vector<std::string>& members) {
  Names inside, seen;
  for (const auto& m : members) collect(p, m, inside, seen);
  return inside;
}

bool is_wormhole(const Patch& p, const std::string& id) {
  const Node* n = node_named(p, id);
  return n && n->type == "Wormhole";
}

// One source leaving: added once per (node, tap); counted when any feed or
// the output reads it, unless it is a face's node.
void leave(std::vector<Leaving>& out, const Names& faceNodes, const std::string& node, bool tap, bool counts) {
  const bool count = counts && !faceNodes.count(node);
  for (auto& l : out)
    if (l.node == node && l.tap == tap) {
      l.counted = l.counted || count;
      return;
    }
  out.push_back(Leaving{node, tap, count});
}

Boundary boundary_of(const Patch& p, const Names& inside) {
  Boundary b;
  const Names faceNodes = face_nodes(p);
  for (const auto& n : p.graph.nodes) {
    const bool in = inside.count(n.id) != 0;
    for (const auto& [pin, feeds] : n.wires)
      for (std::size_t k = 0; k < feeds.size(); ++k) {
        const Wire& w = feeds[k];
        if (in == (inside.count(w.source) != 0)) continue;
        if (n.type == "Wormhole" && is_wormhole(p, w.source)) continue;
        if (in) b.in.push_back(Crossing{n.id, pin, k, w.source, w.tap, false});
        else leave(b.out, faceNodes, w.source, w.tap, true);
      }
    for (const auto& [setting, source] : n.dynamicPins) {
      if (in == (inside.count(source) != 0)) continue;
      if (in) b.in.push_back(Crossing{n.id, setting, 0, source, false, true});
      else leave(b.out, faceNodes, source, false, false);
    }
  }
  if (inside.count(p.graph.output)) leave(b.out, faceNodes, p.graph.output, false, true);
  return b;
}

std::vector<std::string> counted_outputs(const Patch& p, const Boundary& b) {
  Names counted;
  for (const auto& l : b.out)
    if (l.counted) counted.insert(l.node);
  std::vector<std::string> out;
  for (const auto& n : p.graph.nodes)
    if (counted.count(n.id)) out.push_back(n.id);
  return out;
}

// The patch's nodes in signal order: each after every node it reads
// without a tap (its untapped feeds and its drives); a loop's nodes last.
std::vector<std::string> signal_order(const Patch& p) {
  std::vector<std::string> order;
  Names done;
  for (bool moved = true; moved;) {
    moved = false;
    for (const auto& n : p.graph.nodes) {
      if (done.count(n.id)) continue;
      bool ready = true;
      for (const auto& [pin, feeds] : n.wires)
        for (const auto& w : feeds) ready = ready && (w.tap || done.count(w.source) || !node_named(p, w.source));
      for (const auto& [setting, source] : n.dynamicPins) ready = ready && (done.count(source) || !node_named(p, source));
      if (!ready) continue;
      done.insert(n.id);
      order.push_back(n.id);
      moved = true;
    }
  }
  for (const auto& n : p.graph.nodes)
    if (!done.count(n.id)) order.push_back(n.id);
  return order;
}

bool is_signal(const Node& n) {
  const TypeDeclarations* d = type_declarations(n.type);
  return d && d->signal;
}

}  // namespace

Boundary boundary(const Patch& patch, std::string_view group) {
  return boundary_of(patch, under_all(patch, {std::string(group)}));
}

std::vector<std::string> nodes_under(const Patch& patch, std::string_view group) {
  const Names inside = under_all(patch, {std::string(group)});
  std::vector<std::string> ids;
  for (const auto& n : patch.graph.nodes)
    if (inside.count(n.id)) ids.push_back(n.id);
  return ids;
}

std::string group_holding(const Patch& patch, std::string_view member) {
  for (const auto& g : patch.layout.groups)
    if (std::find(g.members.begin(), g.members.end(), member) != g.members.end()) return g.name;
  return {};
}

std::vector<std::string> path_to(const Patch& patch, std::string_view thing) {
  std::vector<std::string> path;
  for (std::string at = group_holding(patch, thing); !at.empty(); at = group_holding(patch, at)) {
    if (std::find(path.begin(), path.end(), at) != path.end()) break;   // a loop check() refuses: no endless walk
    path.insert(path.begin(), at);
  }
  return path;
}

std::string output_node(const Patch& patch, std::string_view group) {
  const Names inside = under_all(patch, {std::string(group)});
  const std::vector<std::string> outs = counted_outputs(patch, boundary_of(patch, inside));
  if (outs.size() > 1) return {};
  if (outs.size() == 1) {
    const Node* n = node_named(patch, outs[0]);
    return n && is_signal(*n) ? outs[0] : std::string();
  }
  const Names faceNodes = face_nodes(patch);
  std::string last;
  for (const auto& id : signal_order(patch)) {
    const Node* n = node_named(patch, id);
    if (inside.count(id) && !faceNodes.count(id) && n && is_signal(*n)) last = id;
  }
  return last;
}

std::vector<std::string> group_outputs(const Patch& patch, const std::vector<std::string>& members) {
  return counted_outputs(patch, boundary_of(patch, under_all(patch, members)));
}

std::string group_refusal(const Patch& patch, const std::vector<std::string>& members) {
  if (members.empty()) return "nothing selected";
  const Names faceNodes = face_nodes(patch);
  std::vector<std::string> groupable;
  for (const auto& m : members)
    if (group_named(patch, m) || face_named(patch, m) || (node_named(patch, m) && !faceNodes.count(m)))
      groupable.push_back(m);
  if (groupable.empty()) return "nothing groupable selected";
  const std::vector<std::string> outs = group_outputs(patch, groupable);
  if (outs.size() <= 1) return {};
  std::string words = "selection has " + std::to_string(outs.size()) + " outputs:";
  for (const auto& o : outs) words += " " + o;
  return words;
}

std::vector<std::string> shown_at(const Patch& patch, const std::vector<std::string>& path) {
  const std::string holder = path.empty() ? std::string() : path.back();
  const Names faceNodes = face_nodes(patch);
  std::vector<std::string> shown;
  for (const auto& n : patch.graph.nodes)
    if (!faceNodes.count(n.id) && group_holding(patch, n.id) == holder) shown.push_back(n.id);
  for (const auto& f : patch.layout.faces)
    if (group_holding(patch, f.label) == holder) shown.push_back(f.label);
  for (const auto& g : patch.layout.groups)
    if (group_holding(patch, g.name) == holder) shown.push_back(g.name);
  return shown;
}

std::string shown_for(const Patch& patch, const std::vector<std::string>& path, std::string_view thing) {
  std::string at(thing);
  for (const auto& f : patch.layout.faces)
    for (const auto& [field, id] : f.fields)
      if (id == thing) at = f.label;
  const std::vector<std::string> chain = path_to(patch, at);
  if (chain.size() < path.size() || !std::equal(path.begin(), path.end(), chain.begin())) return {};
  return chain.size() == path.size() ? at : chain[path.size()];
}

std::string wormhole_twin(const Patch& patch, std::string_view node) {
  const Node* self = node_named(patch, node);
  if (!self || self->type != "Wormhole") return {};
  for (const auto& n : patch.graph.nodes)
    if (n.type == "Wormhole" && n.id != node)
      for (const auto& [pin, feeds] : n.wires)
        for (const auto& w : feeds)
          if (w.source == node) return n.id;
  for (const auto& [pin, feeds] : self->wires)
    for (const auto& w : feeds)
      if (w.source != node && is_wormhole(patch, w.source)) return w.source;
  return {};
}

std::size_t valid_depth(const Patch& patch, const std::vector<std::string>& path) {
  std::string holder;
  for (std::size_t d = 0; d < path.size(); ++d) {
    if (!group_named(patch, path[d]) || group_holding(patch, path[d]) != holder) return d;
    holder = path[d];
  }
  return path.size();
}

}  // namespace mforce::ui
