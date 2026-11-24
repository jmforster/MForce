#include "mforce/ui_model/placement.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "mforce/patch/type_declarations.h"

namespace mforce::ui {

namespace {

// The canvas units of the layout: a column's width, and a slot as a header,
// a fixed height per row, and a gap below it. A node's rows are its declared
// Params and Inputs, each setting a dynamic pin feeds, and its output; a
// face's are the note's six fields. Fixed, because this module cannot
// measure what the views draw.
constexpr float kColumnWidth = 280.0f;
constexpr float kHeader = 40.0f;
constexpr float kPinHeight = 22.0f;
constexpr float kSlotGap = 20.0f;
constexpr float kFaceRows = 6.0f;
// How far below the placed things a block of newly placed ones starts.
constexpr float kBlockGap = 200.0f;
// A coordinate this far out is a fault in the file, not a place on the canvas.
constexpr float kFarthest = 1.0e7f;

// The ids a node reads: its feeds, taps among them, and its dynamic pins' sources.
std::vector<std::string> reads(const Node& n) {
  std::vector<std::string> ids;
  for (const auto& [pin, feeds] : n.wires)
    for (const auto& w : feeds) ids.push_back(w.source);
  for (const auto& [setting, source] : n.dynamicPins) ids.push_back(source);
  return ids;
}

// Each node's column, by its index in the graph: the output's is 0, and a
// node first reached from a node of column k is in k + 1 (a breadth-first
// walk, so the nearest reader decides and a loop ends the walk). A node the
// walk never reaches is in `unreached`, the column after the last.
struct Columns {
  std::vector<int> col;
  int unreached{0};
};

Columns columns(const Graph& g) {
  std::map<std::string, size_t> index;                  // an id's first node
  for (size_t i = 0; i < g.nodes.size(); ++i) index.try_emplace(g.nodes[i].id, i);
  Columns c{std::vector<int>(g.nodes.size(), -1), 0};
  std::deque<size_t> pending;
  if (auto out = index.find(g.output); out != index.end()) {
    c.col[out->second] = 0;
    pending.push_back(out->second);
  }
  int last = 0;
  while (!pending.empty()) {
    const size_t i = pending.front();
    pending.pop_front();
    last = std::max(last, c.col[i]);
    for (const auto& id : reads(g.nodes[i])) {
      auto it = index.find(id);
      if (it == index.end() || c.col[it->second] >= 0) continue;
      c.col[it->second] = c.col[i] + 1;
      pending.push_back(it->second);
    }
  }
  const bool anyReached = std::any_of(c.col.begin(), c.col.end(), [](int k) { return k >= 0; });
  c.unreached = anyReached ? last + 1 : 0;
  for (int& k : c.col)
    if (k < 0) k = c.unreached;
  return c;
}

float slot_height(const Node& n) {
  const TypeDeclarations* d = type_declarations(n.type);
  const size_t inputs = (d ? d->params.size() + d->inputs.size() : 0) + n.dynamicPins.size();
  return kHeader + kPinHeight * float(inputs + 1) + kSlotGap;
}

// One thing the canvas draws: a node no face names, or a face.
struct Thing {
  std::optional<size_t> node;    // the node's index in the graph
  std::optional<size_t> face;    // the face's index in the layout
  int column{0};
  size_t order{0};               // top to bottom in a column: the graph index of its (first) node
  float height{0};
};

std::set<std::string> face_nodes(const Layout& l) {
  std::set<std::string> ids;
  for (const auto& f : l.faces)
    for (const auto& [field, id] : f.fields) ids.insert(id);
  return ids;
}

// A face's column is its nodes' nearest; its order its first node's.
Thing face_thing(const Patch& p, const Columns& c, size_t f) {
  Thing t{std::nullopt, f, c.unreached, p.graph.nodes.size() + f, kHeader + kPinHeight * kFaceRows + kSlotGap};
  bool any = false;
  for (const auto& [field, id] : p.layout.faces[f].fields)
    for (size_t i = 0; i < p.graph.nodes.size(); ++i) {
      if (p.graph.nodes[i].id != id) continue;
      t.column = any ? std::min(t.column, c.col[i]) : c.col[i];
      t.order = std::min(t.order, i);
      any = true;
      break;
    }
  return t;
}

// The things with no position yet, in the order they are laid out.
std::vector<Thing> unplaced_things(const Patch& p) {
  const Columns c = columns(p.graph);
  const std::set<std::string> named = face_nodes(p.layout);
  std::vector<Thing> out;
  std::set<std::string> seen;
  for (size_t i = 0; i < p.graph.nodes.size(); ++i) {
    const Node& n = p.graph.nodes[i];
    if (!seen.insert(n.id).second || named.count(n.id) || p.layout.positions.count(n.id)) continue;
    out.push_back(Thing{i, std::nullopt, c.col[i], i, slot_height(n)});
  }
  for (size_t f = 0; f < p.layout.faces.size(); ++f)
    if (!p.layout.faces[f].pos) out.push_back(face_thing(p, c, f));
  std::stable_sort(out.begin(), out.end(), [](const Thing& a, const Thing& b) { return a.order < b.order; });
  return out;
}

// Positions for `things`, by column, with column 0's top at the origin and
// further columns to the left.
std::vector<Vec2> lay_out(const std::vector<Thing>& things) {
  std::map<int, float> nextY;
  std::vector<Vec2> out;
  for (const Thing& t : things) {
    float& y = nextY[t.column];
    out.push_back(Vec2{-kColumnWidth * float(t.column), y});
    y += t.height;
  }
  return out;
}

// Every point already placed: the nodes' positions and the faces'.
std::vector<Vec2> placed_points(const Layout& l) {
  std::vector<Vec2> points;
  for (const auto& [id, pos] : l.positions) points.push_back(pos);
  for (const auto& f : l.faces)
    if (f.pos) points.push_back(*f.pos);
  return points;
}

// Moves `block` so its left edge is the placed points' left edge and its top
// sits kBlockGap below the lowest of them.
void below_the_placed(std::vector<Vec2>& block, const std::vector<Vec2>& placed) {
  float placedLeft = placed.front().x, placedBottom = placed.front().y;
  for (const Vec2& p : placed) {
    placedLeft = std::min(placedLeft, p.x);
    placedBottom = std::max(placedBottom, p.y);
  }
  float blockLeft = block.front().x, blockTop = block.front().y;
  for (const Vec2& p : block) {
    blockLeft = std::min(blockLeft, p.x);
    blockTop = std::min(blockTop, p.y);
  }
  for (Vec2& p : block) {
    p.x += placedLeft - blockLeft;
    p.y += placedBottom + kBlockGap - blockTop;
  }
}

bool on_the_canvas(Vec2 p) {
  return std::isfinite(p.x) && std::isfinite(p.y) && std::fabs(p.x) < kFarthest && std::fabs(p.y) < kFarthest;
}

// A position no canvas can draw is no position: the thing is placed afresh.
void forget_faulty_positions(Layout& l) {
  std::erase_if(l.positions, [](const auto& entry) { return !on_the_canvas(entry.second); });
  for (auto& f : l.faces)
    if (f.pos && !on_the_canvas(*f.pos)) f.pos.reset();
  if (l.outputPos && !on_the_canvas(*l.outputPos)) l.outputPos.reset();
}

}  // namespace

int place_unplaced_nodes(Patch& patch) {
  forget_faulty_positions(patch.layout);
  const std::vector<Thing> things = unplaced_things(patch);
  int placed = 0;
  if (!things.empty()) {
    std::vector<Vec2> block = lay_out(things);
    const std::vector<Vec2> already = placed_points(patch.layout);
    if (!already.empty()) below_the_placed(block, already);
    for (size_t k = 0; k < things.size(); ++k) {
      if (things[k].node) patch.layout.positions[patch.graph.nodes[*things[k].node].id] = block[k];
      else patch.layout.faces[*things[k].face].pos = block[k];
    }
    placed += int(things.size());
  }
  if (!patch.layout.outputPos) {
    auto out = patch.layout.positions.find(patch.graph.output);
    const Vec2 at = out == patch.layout.positions.end() ? Vec2{} : out->second;
    patch.layout.outputPos = Vec2{at.x + kColumnWidth, at.y};
    ++placed;
  }
  return placed;
}

int arrange_nodes(Patch& patch) {
  patch.layout.positions.clear();
  patch.layout.outputPos.reset();
  patch.layout.panning.reset();
  for (auto& f : patch.layout.faces) f.pos.reset();
  return place_unplaced_nodes(patch) - 1;             // less the Output node
}

}  // namespace mforce::ui
