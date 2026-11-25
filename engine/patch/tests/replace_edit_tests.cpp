// replace_node: what is carried by name, what has no place and is named,
// and what follows the new id.
#include "edit_fixture.h"
#include <algorithm>
#include <string>
#include <vector>

using namespace edit_fixture;

namespace {

using Names = std::vector<std::string>;

// A patch with an instrument block and one node of `type` under `id`, made
// as add_node makes it.
Patch one(const char* type, const char* id = "x") {
  Patch p;
  p.instrument = InstrumentBlock{};
  (void)add_node(p, type, id, 5u, Vec2{1, 2});
  return p;
}

const Node& only(const Patch& p) { return p.graph.nodes.front(); }

void run_carried_by_name() {
  Patch p = instrument();
  Node noise = node("noise", "WhiteNoiseSource");
  noise.seed = 7u;
  noise.values["amplitude"] = 0.5f;
  Node filt = node("filt", "SVFSource");
  filt.wires["source"] = {Wire{"noise"}};
  p.graph.nodes.push_back(noise);
  p.graph.nodes.push_back(filt);
  p.layout.positions["noise"] = Vec2{5, 5};
  p.layout.groups = {Group{"G", {"noise", "filt"}, Vec2{}}};
  const Replaced r = replace_node(p, "noise", "RedNoiseSource");
  CHECK(r.id == "RedNoise" && r.wiresKept == 1 && r.dropped.empty());
  const Node& made = p.graph.nodes[7];
  CHECK(made.id == "RedNoise" && made.type == "RedNoiseSource" && made.seed == 7u);
  CHECK(made.values.at("amplitude") == 0.5f && made.values.at("frequency") == default_of("RedNoiseSource", "frequency"));
  CHECK(p.graph.nodes[8].wires.at("source") == (std::vector<Wire>{Wire{"RedNoise"}}));
  CHECK(p.layout.positions.at("RedNoise") == (Vec2{5, 5}) && !p.layout.positions.count("noise"));
  CHECK(p.layout.groups[0].members == (Names{"RedNoise", "filt"}) && p.check().empty());

  Patch back = p;
  at(back, "RedNoise").values["frequency"] = 200.0f;
  const Replaced lost = replace_node(back, "RedNoise", "WhiteNoiseSource");
  CHECK(lost.id == "WhiteNoise" && lost.dropped == (Names{"frequency"}));   // phase and the rest at their defaults

  Patch env = one("Envelope");
  at(env, "x").arrays["startVal"][0] = 0.25f;
  const Node before = only(env);
  (void)replace_node(env, "x", "Envelope");
  CHECK(only(env).id == "Envelope" && only(env).arrays == before.arrays);   // the same columns: carried whole
}

void run_settings() {
  Patch p = one("Envelope");
  at(p, "x").settings["timeMode"] = 1.0f;                 // "seconds"
  Replaced r = replace_node(p, "x", "SegmentSource");
  CHECK(only(p).settings.at("timeMode") == 2.0f);         // "seconds" there, by its label
  CHECK(r.dropped == (Names{"arrays"}));                   // the stage columns are not SegmentSource's

  p = one("Biquad");
  at(p, "x").settings["mode"] = 1.0f;                     // "Resonance": no such label on SVFSource
  r = replace_node(p, "x", "SVFSource");
  CHECK(only(p).settings.at("mode") == 0.0f && r.dropped == (Names{"mode"}));

  p = one("TargetEvolution");
  at(p, "x").settings["holdCycles"] = 60.0f;              // outside HybridKSSource's 1 to 50
  r = replace_node(p, "x", "HybridKSSource");
  CHECK(std::find(r.dropped.begin(), r.dropped.end(), "holdCycles") != r.dropped.end());
  CHECK(only(p).settings.at("holdCycles") == 5.0f && p.check().empty());
  p = one("TargetEvolution");
  at(p, "x").settings["holdCycles"] = 20.0f;
  (void)replace_node(p, "x", "HybridKSSource");
  CHECK(only(p).settings.at("holdCycles") == 20.0f);

  p = one("NameGate");
  at(p, "x").strings["name"] = "lead";
  (void)replace_node(p, "x", "NameGate");
  CHECK(only(p).strings.at("name") == "lead");
  r = replace_node(p, "NameGate", "SineSource");
  CHECK(r.dropped == (Names{"name"}));
}

void run_wires_and_drives() {
  Patch p = instrument();
  Replaced r = replace_node(p, "env", "SineSource");      // env: maxValue 0.8, timeScale driven by freq
  CHECK(r.id == "Sine" && r.wiresKept == 1 && r.dropped == (Names{"maxValue", "timeScale"}));
  CHECK(at(p, "osc").wires.at("amplitude") == (std::vector<Wire>{Wire{"Sine"}}) && p.check().empty());

  p = instrument();
  r = replace_node(p, "env", "Envelope");
  CHECK(at(p, "Envelope").dynamicPins.at("timeScale") == "freq" && at(p, "Envelope").values.at("maxValue") == 0.8f);
  CHECK(r.dropped.empty() && r.wiresKept == 1);

  p = instrument();
  r = replace_node(p, "osc", "WhiteNoiseSource");          // no frequency pin; the output follows
  CHECK(r.id == "WhiteNoise" && p.graph.output == "WhiteNoise");
  CHECK(r.dropped == (Names{"frequency"}) && r.wiresKept == 1);
  CHECK(at(p, "WhiteNoise").wires.at("amplitude") == (std::vector<Wire>{Wire{"env"}}) && p.check().empty());

  p = instrument();
  r = replace_node(p, "osc", "ExplicitPartials");          // a part: no output, and its feeds have no place
  CHECK(p.graph.output.empty() && std::find(r.dropped.begin(), r.dropped.end(), "output") != r.dropped.end());
  CHECK(p.check().empty());

  p = instrument();
  at(p, "wt").wires["evolution"] = {Wire{"pe"}};
  r = replace_node(p, "pe", "SineSource");                 // the slot does not take a sine
  CHECK(r.dropped == (Names{"wt.evolution"}) && r.wiresKept == 0 && !at(p, "wt").wires.count("evolution"));
  CHECK(p.check().empty());
}

void run_replace_refusals() {
  expect_refused("no node to replace", instrument(), [](Patch& p) { (void)replace_node(p, "nope", "SineSource"); },
                 "'nope' names no node: there is nothing to replace");
  Patch faced = instrument();
  faced.layout.faces = {NoteFace{"Note1", {{"frequency", "freq"}}, Vec2{}}};
  expect_refused("a note face's node", faced, [](Patch& p) { (void)replace_node(p, "freq", "SineSource"); },
                 "'freq' is a node of note face 'Note1': it is not replaced");
  expect_refused("a type the registry lacks", instrument(),
                 [](Patch& p) { (void)replace_node(p, "lfo", "NoSuchSource"); }, "Unknown node type: NoSuchSource");
}

}  // namespace

void run_replace_edit_tests() {
  std::printf("replace_node: carried by name, the rest named, everything that named it following\n");
  run_carried_by_name();
  run_settings();
  run_wires_and_drives();
  run_replace_refusals();
}
