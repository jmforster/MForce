// place_unplaced_nodes: the columns, the order in a column, the Output node,
// the block below placed nodes, a loop, sameness, and every measured patch.
#include "mforce/testing/check.h"
#include "mforce/patch/patch_json.h"
#include "mforce/source/register_all_sources.h"
#include "mforce/ui_model/placement.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <utility>

using namespace mforce;
using namespace mforce::ui;

namespace {

Node node(const char* id, const char* type) {
  Node n;
  n.id = id;
  n.type = type;
  return n;
}

// osc is the output and reads pf and env; env reads osc through a tap (a
// loop) and has a dynamic pin from slow; lone is read by nothing.
Patch small() {
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

Vec2 at(const Patch& p, const char* id) { return p.layout.positions.at(id); }

bool all_distinct(const Patch& p) {
  std::set<std::pair<float, float>> seen;
  for (const auto& [id, pos] : p.layout.positions)
    if (!seen.insert({pos.x, pos.y}).second) return false;
  return true;
}

}  // namespace

void run_placement_rule_tests() {
  register_all_sources();
  Patch p = small();
  CHECK(place_unplaced_nodes(p) == 6);                    // five nodes and the Output node
  CHECK(at(p, "osc").x == 0.0f && at(p, "osc").y == 0.0f);
  CHECK(at(p, "pf").x < at(p, "osc").x);                  // column 1, left of the output
  CHECK(at(p, "env").x == at(p, "pf").x);                 // the same column
  CHECK(at(p, "env").y > at(p, "pf").y);                  // the graph's order, top to bottom
  CHECK(at(p, "slow").x < at(p, "env").x);                // column 2, through a dynamic pin
  CHECK(at(p, "lone").x < at(p, "slow").x);               // read by nothing: the last column
  CHECK(at(p, "lone").y == 0.0f && at(p, "slow").y == 0.0f);
  REQUIRE(p.layout.outputPos.has_value());
  CHECK(p.layout.outputPos->x > at(p, "osc").x && p.layout.outputPos->y == at(p, "osc").y);
  CHECK(all_distinct(p));
  CHECK(p.check().empty());

  Patch again = small();
  (void)place_unplaced_nodes(again);
  CHECK(again == p);                                      // the same patch, the same positions
  CHECK(place_unplaced_nodes(p) == 0);                    // nothing left to place
}

void run_placement_block_tests() {
  Patch p = small();
  p.layout.positions = {{"osc", Vec2{100, 50}}, {"pf", Vec2{-200, 300}}};
  p.layout.outputPos = Vec2{400, 50};
  CHECK(place_unplaced_nodes(p) == 3);
  CHECK(at(p, "osc") == (Vec2{100, 50}));                 // placed nodes keep theirs
  CHECK(at(p, "pf") == (Vec2{-200, 300}));
  CHECK(*p.layout.outputPos == (Vec2{400, 50}));
  float left = 1e9f;
  for (const char* id : {"env", "slow", "lone"}) {
    CHECK(at(p, id).y > 300.0f);                          // below the placed nodes
    left = std::min(left, at(p, id).x);
  }
  CHECK(left == -200.0f);                                 // the block starts at their left edge
  CHECK(all_distinct(p));

  // A loop of wires (check() refuses it; placement must still end).
  Patch loop;
  Node a = node("a", "SineSource"), b = node("b", "SineSource");
  a.wires["amplitude"] = {Wire{"b"}};
  b.wires["amplitude"] = {Wire{"a"}};
  loop.graph.nodes = {a, b};
  loop.graph.output = "a";
  CHECK(place_unplaced_nodes(loop) == 3);
  CHECK(all_distinct(loop));
}

void run_placement_census_tests() {
  int patches = 0, placed = 0, hadNone = 0, lacked = 0, shared = 0;
  for (const char* folder : {"patches/library", "patches/baselines"})
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
      const std::string file = entry.path().generic_string();
      const Patch before = load_patch(file);
      Patch p = before;
      ++patches;
      if (before.layout.positions.empty()) ++hadNone;
      else if (before.layout.positions.size() < before.graph.nodes.size()) ++lacked;
      placed += place_unplaced_nodes(p);
      bool every = true, kept = true;
      for (const auto& n : p.graph.nodes) every = every && p.layout.positions.count(n.id) != 0;
      for (const auto& [id, pos] : before.layout.positions) kept = kept && p.layout.positions.at(id) == pos;
      if (!all_distinct(p)) {
        ++shared;
        std::printf("  %s: two nodes share a position\n", file.c_str());
      }
      CHECK(every);
      CHECK(kept);
      CHECK(p.layout.outputPos.has_value());
      CHECK(place_unplaced_nodes(p) == 0);
      CHECK(p.check().empty());
    }
  std::printf("  placement: %d patches (%d with no position, %d lacking some), %d positions placed, "
              "%d with a shared position\n", patches, hadNone, lacked, placed, shared);
  CHECK(patches > 0);
  CHECK(shared == 0);
}
