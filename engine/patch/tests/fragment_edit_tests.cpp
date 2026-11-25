// A fragment cut from a patch (fragment_of), put into one (paste), and
// carried as text (fragment_text, fragment_from_text).
#include "edit_fixture.h"
#include "mforce/patch/fragment_text.h"
#include "mforce/patch/patch_json.h"
#include <string>
#include <vector>

using namespace edit_fixture;

namespace {

// The fixture's instrument with a seed on env and osc.
Patch seeded() {
  Patch p = instrument();
  at(p, "env").seed = 11u;
  at(p, "osc").seed = 12u;
  return p;
}

// A patch with an instrument block and these nodes, a sine each.
Patch holding(const std::vector<const char*>& ids) {
  Patch p;
  p.instrument = InstrumentBlock{};
  for (const char* id : ids) p.graph.nodes.push_back(node(id, "SineSource"));
  return p;
}

void run_paste_into_same() {
  const Patch from = seeded();
  const Fragment f = fragment_of(from, {"env", "osc"});
  CHECK(f.nodes.size() == 2 && f.nodes[0] == from.graph.nodes[1] && f.nodes[1] == from.graph.nodes[2]);
  CHECK(f.positions.at("env") == (Vec2{0, 100}) && f.positions.at("osc") == (Vec2{200, 50}));
  CHECK(f.groups.empty() && f.faces.empty());

  Patch want = from;
  Node env2 = at(want, "env");
  env2.id = "env2";
  Node osc2 = at(want, "osc");
  osc2.id = "osc2";
  osc2.wires["amplitude"] = {Wire{"env2"}};              // a feed among the pasted follows; freq is kept
  want.graph.nodes.push_back(env2);
  want.graph.nodes.push_back(osc2);
  want.layout.positions["env2"] = Vec2{60, 160};
  want.layout.positions["osc2"] = Vec2{260, 110};
  Patch p = from;
  const Pasted done = paste(p, f, Vec2{60, 60});
  CHECK(done.ids == (std::vector<std::string>{"env2", "osc2"}) && done.dropped == 0);
  if (!(p == want)) std::printf("  paste into the same patch: not the patch expected\n");
  CHECK(p == want && p.check().empty());
}

void run_paste_into_another() {
  const Fragment f = fragment_of(seeded(), {"env", "osc"});
  Patch p = holding({"lfo"});
  const Pasted done = paste(p, f, Vec2{});
  CHECK(done.ids == (std::vector<std::string>{"env", "osc"}));
  CHECK(done.dropped == 2);                                  // osc's freq feed and env's freq drive
  const Node& osc = p.graph.nodes[2];
  CHECK(!osc.wires.count("frequency") && osc.values.at("frequency") == default_of("SineSource", "frequency"));
  CHECK(osc.wires.at("amplitude") == (std::vector<Wire>{Wire{"env"}}) && osc.seed == 12u);
  CHECK(p.graph.nodes[1].dynamicPins.empty());

  Patch freqHere = holding({"freq"});
  freqHere.graph.nodes[0].type = "PerformNode";
  const Pasted kept = paste(freqHere, f, Vec2{});
  CHECK(kept.dropped == 0 && freqHere.graph.nodes[2].wires.at("frequency") == (std::vector<Wire>{Wire{"freq"}}));
}

void run_pasted_names() {
  const auto one = [](const char* id) {
    Fragment f;
    f.nodes = {node(id, "SineSource")};
    return f;
  };
  Patch p = holding({"Sine2"});
  CHECK(paste(p, one("Sine2"), Vec2{}).ids == (std::vector<std::string>{"Sine3"}));
  p = holding({"BP0", "BP1", "BP2", "BP3"});
  CHECK(paste(p, one("BP0"), Vec2{}).ids == (std::vector<std::string>{"BP4"}));
  p = holding({"Reverb"});
  CHECK(paste(p, one("Reverb"), Vec2{}).ids == (std::vector<std::string>{"Reverb2"}));
  p = holding({"42"});
  CHECK(paste(p, one("42"), Vec2{}).ids == (std::vector<std::string>{"422"}));
  p = holding({});
  Fragment twice;
  twice.nodes = {node("osc", "SineSource"), node("osc", "SineSource")};
  CHECK(paste(p, twice, Vec2{}).ids == (std::vector<std::string>{"osc", "osc2"}));
}

void run_fragment_refusals() {
  expect_refused("no node to copy", instrument(), [](Patch& p) { (void)fragment_of(p, {"nope"}); },
                 "'nope' names no node: it cannot be copied");
  Patch faced = instrument();
  faced.layout.faces = {NoteFace{"Note1", {{"frequency", "freq"}}, Vec2{}}};
  expect_refused("a note face's node", faced, [](Patch& p) { (void)fragment_of(p, {"freq"}); },
                 "'freq' is a node of note face 'Note1': a note face's node is not copied");
  expect_refused("an empty fragment", instrument(), [](Patch& p) { (void)paste(p, Fragment{}, Vec2{}); },
                 "the fragment holds no nodes");
  Fragment grouped;
  grouped.nodes = {node("x", "SineSource")};
  grouped.groups = {Group{"G", {"x"}, Vec2{}}};
  expect_refused("a fragment with a group", instrument(), [&](Patch& p) { (void)paste(p, grouped, Vec2{}); },
                 "groups or note faces");
  Fragment unknown;
  unknown.nodes = {node("x", "NoSuchSource")};
  expect_refused("a paste check() refuses", instrument(), [&](Patch& p) { (void)paste(p, unknown, Vec2{}); },
                 "Unknown node type: NoSuchSource");
}

// The non-face nodes of a patch, as fragment_of takes them.
std::vector<std::string> plain_nodes(const Patch& p) {
  std::vector<std::string> ids;
  for (const auto& n : p.graph.nodes) {
    bool face = false;
    for (const auto& f : p.layout.faces)
      for (const auto& [field, id] : f.fields) face = face || id == n.id;
    if (!face) ids.push_back(n.id);
  }
  return ids;
}

void run_fragment_text() {
  for (const char* file : {"patches/library/winds/oboe1.json", "patches/library/keys/acoustic_piano/piano_default.json",
                           "patches/baselines/feedback/loop_hysteresis_bow.json"}) {
    const Patch p = load_patch(file);
    const Fragment f = fragment_of(p, plain_nodes(p));
    const std::string text = fragment_text(f);
    CHECK(text.find('\n') == std::string::npos && text.find("\"mforceClip\":1") != std::string::npos);
    if (!(fragment_from_text(text) == f)) std::printf("  %s: the fragment's text does not read back\n", file);
    CHECK(fragment_from_text(text) == f);
  }
  const auto refusal = [](const std::string& text) {
    try {
      (void)fragment_from_text(text);
    } catch (const std::exception& e) {
      return std::string(e.what());
    }
    return std::string("(read)");
  };
  CHECK(refusal("hello") == "clipboard is not an MForce fragment");
  CHECK(refusal(R"J({"nodes":[{"id":"x","type":"SineSource"}]})J") == "clipboard is not an MForce fragment");
  CHECK(refusal(R"J({"mforceClip":2,"nodes":[]})J") == "clipboard is not an MForce fragment");
  CHECK(refusal(R"J({"mforceClip":1,"nodes":[]})J") == "fragment had no nodes");
  CHECK(refusal(R"J({"mforceClip":1})J") == "fragment had no nodes");
  CHECK(refusal(R"J({"mforceClip":1,"mode":"patch","nodes":[{"id":"x","type":"SineSource"}]})J").find(
            "unknown key 'mode'") != std::string::npos);
  CHECK(refusal(R"J({"mforceClip":1,"nodes":[{"id":"x","type":"NoSuchSource"}]})J").find(
            "Unknown node type: NoSuchSource") != std::string::npos);
}

}  // namespace

void run_fragment_edit_tests() {
  std::printf("fragments: cut, pasted into the same patch and another, fresh names, refusals, the text\n");
  run_paste_into_same();
  run_paste_into_another();
  run_pasted_names();
  run_fragment_refusals();
  run_fragment_text();
}
