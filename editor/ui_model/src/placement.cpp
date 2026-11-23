#include "mforce/ui_model/placement.h"
#include <algorithm>
#include <cstddef>
#include <deque>
#include <map>
#include <string>
#include <vector>
#include "mforce/patch/type_declarations.h"

namespace mforce::ui {

namespace {

// The canvas units of the layout: a column's width, and a node's slot as a
// header plus a fixed height per declared pin, with a gap below it. Fixed,
// because this module cannot measure what the views draw.
constexpr float kColumnWidth = 280.0f;
constexpr float kHeader = 40.0f;
constexpr float kPinHeight = 22.0f;
constexpr float kSlotGap = 20.0f;
// How far below the placed nodes a block of newly placed ones starts.
constexpr float kBlockGap = 200.0f;

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
// walk never reaches is in the column after the last.
std::vector<int> columns(const Graph& g) {
  std::map<std::string, size_t> index;                  // an id's first node
  for (size_t i = 0; i < g.nodes.size(); ++i) index.try_emplace(g.nodes[i].id, i);
  std::vector<int> col(g.nodes.size(), -1);
  std::deque<size_t> pending;
  if (auto out = index.find(g.output); out != index.end()) {
    col[out->second] = 0;
    pending.push_back(out->second);
  }
  int last = 0;
  while (!pending.empty()) {
    const size_t i = pending.front();
    pending.pop_front();
    last = std::max(last, col[i]);
    for (const auto& id : reads(g.nodes[i])) {
      auto it = index.find(id);
      if (it == index.end() || col[it->second] >= 0) continue;
      col[it->second] = col[i] + 1;
      pending.push_back(it->second);
    }
  }
  const bool anyReached = std::any_of(col.begin(), col.end(), [](int c) { return c >= 0; });
  const int unreached = anyReached ? last + 1 : 0;
  for (int& c : col)
    if (c < 0) c = unreached;
  return col;
}

float slot_height(const Node& n) {
  const TypeDeclarations* d = type_declarations(n.type);
  const size_t pins = d ? d->params.size() + d->inputs.size() : 0;
  return kHeader + kPinHeight * float(pins) + kSlotGap;
}

// Positions for the nodes `which` names (indices, in the graph's order), by
// column, with column 0's top at the origin and further columns to the left.
std::vector<Vec2> lay_out(const Graph& g, const std::vector<int>& col, const std::vector<size_t>& which) {
  std::map<int, float> nextY;
  std::vector<Vec2> out;
  for (size_t i : which) {
    float& y = nextY[col[i]];
    out.push_back(Vec2{-kColumnWidth * float(col[i]), y});
    y += slot_height(g.nodes[i]);
  }
  return out;
}

// Moves `block` so its left edge is the placed nodes' left edge and its top
// sits kBlockGap below the lowest placed node.
void below_the_placed(std::vector<Vec2>& block, const std::map<std::string, Vec2>& placed) {
  float placedLeft = placed.begin()->second.x, placedBottom = placed.begin()->second.y;
  for (const auto& [id, p] : placed) {
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

}  // namespace

int place_unplaced_nodes(Patch& patch) {
  const Graph& g = patch.graph;
  auto& positions = patch.layout.positions;
  std::vector<size_t> unplaced;
  std::map<std::string, bool> seen;
  for (size_t i = 0; i < g.nodes.size(); ++i)
    if (seen.try_emplace(g.nodes[i].id, true).second && !positions.count(g.nodes[i].id)) unplaced.push_back(i);
  int placed = 0;
  if (!unplaced.empty()) {
    std::vector<Vec2> block = lay_out(g, columns(g), unplaced);
    if (!positions.empty()) below_the_placed(block, positions);
    for (size_t k = 0; k < unplaced.size(); ++k) positions[g.nodes[unplaced[k]].id] = block[k];
    placed += int(unplaced.size());
  }
  if (!patch.layout.outputPos) {
    auto out = positions.find(g.output);
    const Vec2 at = out == positions.end() ? Vec2{} : out->second;
    patch.layout.outputPos = Vec2{at.x + kColumnWidth, at.y};
    ++placed;
  }
  return placed;
}

}  // namespace mforce::ui
