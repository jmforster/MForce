// place_unplaced_nodes: the columns, the order in a column, the Output node,
// the block below placed nodes, a loop, sameness, and every measured patch.
#include "mforce/testing/check.h"
#include "mforce/patch/patch_json.h"
#include "mforce/patch/type_declarations.h"
#include "mforce/source/register_all_sources.h"
#include "mforce/ui_model/note_faces.h"
#include "mforce/ui_model/placement.h"
#include "placement_fixture.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <utility>

using namespace mforce;
using namespace mforce::ui;
using namespace placement_fixture;

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

namespace {

struct Census {
  int patches{0}, placed{0}, hadNone{0}, lacked{0}, shared{0}, overlapping{0};
};

// One measured patch, its note faces gathered first as opening does: every
// node no face names has a position, every face one, no face's node one;
// the positions it had are kept; no two newly placed slots overlap.
void census_of(Census& c, const std::string& file) {
  Patch before = load_patch(file);
  (void)gather_note_faces(before);
  Patch p = before;
  ++c.patches;
  if (before.layout.positions.empty()) ++c.hadNone;
  else if (before.layout.positions.size() < before.graph.nodes.size()) ++c.lacked;
  c.placed += place_unplaced_nodes(p);
  const std::set<std::string> named = face_nodes(p);
  bool every = true, kept = true, faceNodesUnplaced = true, facesPlaced = true;
  for (const auto& n : p.graph.nodes) {
    if (named.count(n.id)) faceNodesUnplaced = faceNodesUnplaced && !p.layout.positions.count(n.id);
    else every = every && p.layout.positions.count(n.id) != 0;
  }
  for (const auto& f : p.layout.faces) facesPlaced = facesPlaced && f.pos.has_value();
  for (const auto& [id, pos] : before.layout.positions) kept = kept && p.layout.positions.at(id) == pos;
  if (!all_distinct(p)) {
    ++c.shared;
    std::printf("  %s: two things share a position\n", file.c_str());
  }
  if (new_slots_overlap(before, p)) {
    ++c.overlapping;
    std::printf("  %s: two newly placed slots overlap in a column\n", file.c_str());
  }
  CHECK(every && kept && faceNodesUnplaced && facesPlaced);
  CHECK(p.layout.outputPos.has_value());
  CHECK(place_unplaced_nodes(p) == 0);
  CHECK(p.check().empty());
}

}  // namespace

void run_placement_census_tests() {
  Census c;
  for (const char* folder : {"patches/library", "patches/baselines"})
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder))
      if (entry.is_regular_file() && entry.path().extension() == ".json") census_of(c, entry.path().generic_string());
  std::printf("  placement: %d patches (%d with no position, %d lacking some), %d positions placed, "
              "%d with a shared position, %d with overlapping slots\n",
              c.patches, c.hadNone, c.lacked, c.placed, c.shared, c.overlapping);
  CHECK(c.patches > 0);
  CHECK(c.shared == 0 && c.overlapping == 0);
}

// A node's slot is its header, a row per declared Param and Input and per
// dynamic pin, a row for its output, and the gap: the second node of a
// column starts that far below the first.
void run_placement_slot_tests() {
  Patch p = small();
  (void)place_unplaced_nodes(p);
  const TypeDeclarations* d = type_declarations("PerformNode");
  REQUIRE(d != nullptr);
  const float rows = float(d->params.size() + d->inputs.size() + 1);
  CHECK(at(p, "env").y - at(p, "pf").y == 40.0f + 22.0f * rows + 20.0f);
}

// Arrange over every measured patch saved with groups or note faces, its
// faces gathered first: every node and face placed afresh, none sharing a
// place, the same layout again on a second call, check() empty, the groups
// and the faces' labels and fields kept, the panning dropped.
void run_arrange_tests() {
  int grouped = 0;
  for (const char* folder : {"patches/library", "patches/baselines"})
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
      Patch before = load_patch(entry.path().generic_string());
      if (before.layout.groups.empty() && before.layout.faces.empty()) continue;
      (void)gather_note_faces(before);
      ++grouped;
      Patch p = before;
      const int drawn = int(p.graph.nodes.size() - face_nodes(p).size() + p.layout.faces.size());
      CHECK(arrange_nodes(p) == drawn);
      Patch fresh = before;
      fresh.layout.positions.clear();
      fresh.layout.outputPos.reset();
      for (auto& f : fresh.layout.faces) f.pos.reset();
      (void)place_unplaced_nodes(fresh);
      CHECK(p.layout.positions == fresh.layout.positions && p.layout.faces == fresh.layout.faces);
      CHECK(all_distinct(p));
      CHECK(!p.layout.panning.has_value());
      CHECK(p.layout.groups == before.layout.groups && p.layout.faces.size() == before.layout.faces.size());
      for (size_t f = 0; f < p.layout.faces.size(); ++f)
        CHECK(p.layout.faces[f].label == before.layout.faces[f].label &&
              p.layout.faces[f].fields == before.layout.faces[f].fields && p.layout.faces[f].pos.has_value());
      CHECK(p.check().empty());
      Patch again = p;
      (void)arrange_nodes(again);
      CHECK(again == p);
    }
  std::printf("  arrange: %d patches saved with groups or note faces\n", grouped);
  CHECK(grouped > 0);
}
