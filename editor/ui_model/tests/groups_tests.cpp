// What a group shows, worked out from the wiring: its boundary, its output,
// the two-output rule's words, and what each level draws; on small made
// patches and on every measured patch that has groups.
#include "mforce/testing/check.h"
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include "mforce/ui_model/groups.h"
#include "mforce/ui_model/open_patch.h"

using namespace mforce;
using namespace mforce::ui;

namespace {

using Names = std::vector<std::string>;

Node sine(const char* id, const char* reads = nullptr, const char* pin = "frequency") {
  Node n;
  n.id = id;
  n.type = "SineSource";
  if (reads) n.wires[pin] = {Wire{reads}};
  return n;
}

// a feeds b; b feeds c and d; c is the output.
Patch chain() {
  Patch p;
  p.instrument = InstrumentBlock{};
  p.graph.nodes = {sine("a"), sine("b", "a"), sine("c", "b", "amplitude"), sine("d", "b")};
  p.graph.output = "c";
  return p;
}

void run_small_boundaries() {
  Patch p = chain();
  p.layout.groups = {Group{"G", {"a", "b"}, Vec2{}}};
  const Boundary b = boundary(p, "G");
  CHECK(b.in.empty() && b.out.size() == 1 && b.out[0].node == "b" && b.out[0].counted);   // two wires, one source
  CHECK(output_node(p, "G") == "b" && nodes_under(p, "G") == (Names{"a", "b"}));
  CHECK(group_refusal(p, {"a", "b"}).empty());
  CHECK(group_refusal(p, {"a", "b", "c"}) == "selection has 2 outputs: b c");
  CHECK(group_outputs(p, {"a", "b", "c"}) == (Names{"b", "c"}));
  CHECK(group_refusal(p, {}) == "nothing selected");
  CHECK(group_refusal(p, {"nope"}) == "nothing groupable selected");
  p.layout.groups = {Group{"H", {"c"}, Vec2{}}};
  const Boundary h = boundary(p, "H");
  CHECK(h.in.size() == 1 && h.in[0].node == "c" && h.in[0].pin == "amplitude" && h.in[0].source == "b");
  CHECK(h.out.size() == 1 && h.out[0].node == "c");          // the patch's output reads it
  CHECK(shown_at(p, {}) == (Names{"a", "b", "d", "H"}) && shown_at(p, {"H"}) == (Names{"c"}));
}

// A span between two Wormholes crosses no edge; a drive and a face's field
// that cross are in the boundary and are not counted.
void run_uncounted_crossings() {
  Patch p = chain();
  Node w1;
  w1.id = "w1";
  w1.type = "Wormhole";
  w1.wires["source"] = {Wire{"a"}};
  Node w2 = w1;
  w2.id = "w2";
  w2.wires["source"] = {Wire{"w1"}};
  Node env;
  env.id = "env";
  env.type = "Envelope";
  env.dynamicPins["timeScale"] = "a";
  Node pf;
  pf.id = "pf";
  pf.type = "PerformNode";
  p.graph.nodes.insert(p.graph.nodes.end(), {w1, w2, env, pf});
  p.graph.nodes[3].wires["frequency"] = {Wire{"w2"}};        // d reads the far half
  p.graph.nodes[1].wires = {{"amplitude", {Wire{"pf"}}}};    // b reads the face's field alone
  p.layout.faces = {NoteFace{"F", {{"frequency", "pf"}}, Vec2{}}};
  p.layout.groups = {Group{"G", {"a", "w1", "F"}, Vec2{}}};
  const Boundary g = boundary(p, "G");
  CHECK(g.in.empty());
  for (const auto& l : g.out) CHECK(!l.counted);             // a: the drive and b; pf: a face's field
  CHECK(group_outputs(p, {"a", "w1", "F"}).empty());
  CHECK(output_node(p, "G") == "w1");                        // none leaves: the last signal, no face's node
  CHECK(shown_at(p, {}) == (Names{"b", "c", "d", "w2", "env", "G"}));
  CHECK(shown_at(p, {"G"}) == (Names{"a", "w1", "F"}));
  p.layout.groups = {Group{"Env", {"env"}, Vec2{}}};
  const Boundary e = boundary(p, "Env");
  CHECK(e.in.size() == 1 && e.in[0].drive && e.in[0].pin == "timeScale" && e.in[0].source == "a");
}

void run_paths() {
  Patch p = chain();
  p.layout.groups = {Group{"Outer", {"Inner", "d"}, Vec2{}}, Group{"Inner", {"a"}, Vec2{}}};
  CHECK(path_to(p, "a") == (Names{"Outer", "Inner"}) && path_to(p, "Outer").empty());
  CHECK(group_holding(p, "Inner") == "Outer" && group_holding(p, "c").empty());
  CHECK(valid_depth(p, {"Outer", "Inner"}) == 2 && valid_depth(p, {"Inner"}) == 0);
  CHECK(valid_depth(p, {"Outer", "nope"}) == 1);
  CHECK(nodes_under(p, "Outer") == (Names{"a", "d"}));
}

}  // namespace

void run_group_tests() {
  std::printf("groups: the boundary, the output, the two-output words, each level\n");
  run_small_boundaries();
  run_uncounted_crossings();
  run_paths();
}

// Every measured patch with groups: each group has exactly one output, its
// shown level holds its direct members, and its in and out are what the
// wiring gives.
void run_group_census_tests() {
  std::printf("groups: every measured patch with groups\n");
  int patches = 0, groups = 0;
  for (const char* folder : {"patches/library", "patches/baselines"})
    for (const auto& e : std::filesystem::recursive_directory_iterator(folder)) {
      if (!e.is_regular_file() || e.path().extension() != ".json") continue;
      const Patch p = open_patch(e.path().generic_string()).patch;
      if (p.layout.groups.empty()) continue;
      ++patches;
      for (const auto& g : p.layout.groups) {
        ++groups;
        Names path = path_to(p, g.name);
        path.push_back(g.name);
        CHECK(shown_at(p, path).size() == g.members.size());
        CHECK(valid_depth(p, path) == path.size());
        const Names outs = group_outputs(p, g.members);
        if (outs.size() != 1) std::printf("  %s: group %s has %zu outputs\n", e.path().generic_string().c_str(), g.name.c_str(), outs.size());
        CHECK(outs.size() == 1 && output_node(p, g.name) == outs[0]);
        CHECK(group_refusal(p, g.members).empty());
      }
      if (p.layout.groups.size() == 3 && p.layout.groups[2].name == "Delay") {   // the winds' and the bow's
        const Boundary d = boundary(p, "Delay");
        CHECK(d.out.size() == 1 && d.out[0].node == "Delay_line" && !d.out[0].tap && d.in.size() == 1);
        CHECK(d.in[0].node == "Delay_line" && d.in[0].source == "Damp_lpf");
        CHECK(boundary(p, "Drive").in.size() == 1 && output_node(p, "Drive") == "Damp_lpf");
      }
    }
  CHECK(patches > 0 && groups > patches);
}
