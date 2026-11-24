// The structural edits of patch/edit.h on the small instrument of
// edit_fixture.h, by hand: each accepted case against the whole record it
// should leave, each refusal by a distinctive part of its sentence with the
// patch left as it was.
#include "patch/tests/edit_fixture.h"
#include "mforce/source/envelope/envelope.h"
#include <limits>
#include <map>
#include <string>
#include <vector>

using namespace mforce;
using namespace edit_fixture;

namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();

// A node of `type` as add_node should make it: every Param at its default,
// every setting at its default, every string setting at its default text.
Node completed(const char* id, const char* type, std::uint32_t seed) {
  Node n = node(id, type);
  n.seed = seed;
  const TypeDeclarations* d = type_declarations(type);
  for (const auto& p : d->params) n.values[p.name] = p.defaultValue;
  for (const auto& s : d->settings) {
    if (s.type == SettingType::String) n.strings[s.name] = s.defaultText;
    else n.settings[s.name] = s.defaultValue;
  }
  return n;
}

Patch plain_sound() {
  Patch p = instrument();
  erase_node(p, "freq");
  at(p, "osc").wires.erase("frequency");
  at(p, "env").dynamicPins.clear();
  p.layout.positions.erase("freq");
  p.instrument.reset();
  return p;
}

}  // namespace

void run_add_node_tests() {
  Patch want = instrument();
  want.graph.nodes.push_back(completed("Sine", "SineSource", 7));
  want.layout.positions["Sine"] = Vec2{10, 20};
  expect_applies("a sine", instrument(), [](Patch& p) { return add_node(p, "SineSource", "Sine", 7, Vec2{10, 20}); },
                 want, Change::Structure);

  Patch curve = instrument();
  CHECK(add_node(curve, "CurveNode", "Curve", 3, Vec2{}) == Change::Structure);
  const std::map<std::string, std::vector<float>> knots = {{"x", {0.0f, 1.0f}}, {"y", {0.0f, 1.0f}}};
  CHECK(at(curve, "Curve").arrays == knots);

  Patch env = instrument();
  CHECK(add_node(env, "Envelope", "Env2", 3, Vec2{}) == Change::Structure);
  const auto adsr = Envelope::make_adsr(0.05f, 0.1f, 0.7f, 0.2f);
  int stages = 0;
  for (const auto& a : type_declarations("Envelope")->arrays) {
    if (a.group != "stages") continue;
    ++stages;
    CHECK(at(env, "Env2").arrays.at(a.name) == adsr->get_array(a.name));
  }
  CHECK(stages > 0 && at(env, "Env2").arrays.size() == size_t(stages));
  CHECK((at(env, "Env2").arrays.at("percent") == std::vector<float>{0.05f, 0.1f, 0.0f, 0.2f}));
  CHECK(env.check().empty());

  expect_refused("an unregistered type", instrument(), [](Patch& p) { add_node(p, "NoSuchSource", "x", 1, Vec2{}); },
                 "node 'x' (NoSuchSource): Unknown node type: NoSuchSource");
  expect_refused("an empty id", instrument(), [](Patch& p) { add_node(p, "SineSource", "", 1, Vec2{}); },
                 "an id is empty");
  expect_refused("a taken id", instrument(), [](Patch& p) { add_node(p, "SineSource", "osc", 1, Vec2{}); },
                 "'osc' names two things: a node, a node");
  expect_refused("a position off the plane", instrument(), [](Patch& p) { add_node(p, "SineSource", "s", 1, Vec2{kInf, 0}); },
                 "node 's' (SineSource): its position (inf, 0) is not two finite numbers");
  expect_refused("a note on a plain sound", plain_sound(), [](Patch& p) { add_node(p, "PerformNode", "n", 1, Vec2{}); },
                 "node 'n' (PerformNode): a plain sound has no note to read");
}

void run_remove_node_tests() {
  Patch want = instrument();
  erase_node(want, "env");
  at(want, "osc").wires.erase("amplitude");
  at(want, "osc").values["amplitude"] = default_of("SineSource", "amplitude");
  want.layout.positions.erase("env");
  expect_applies("a node read by a pin", instrument(), [](Patch& p) { return remove_node(p, "env"); }, want,
                 Change::Structure);

  Patch grouped = instrument();
  grouped.layout.groups = {Group{"G", {"freq", "env"}, Vec2{}}};
  want = grouped;
  erase_node(want, "freq");
  at(want, "osc").wires.erase("frequency");
  at(want, "osc").values["frequency"] = default_of("SineSource", "frequency");
  at(want, "env").dynamicPins.clear();
  want.layout.positions.erase("freq");
  want.layout.groups[0].members = {"env"};
  expect_applies("a node read by a pin and a dynamic pin, in a group", grouped,
                 [](Patch& p) { return remove_node(p, "freq"); }, want, Change::Structure);

  want = instrument();
  erase_node(want, "osc");
  want.graph.output.clear();
  want.layout.positions.erase("osc");
  expect_applies("the output", instrument(), [](Patch& p) { return remove_node(p, "osc"); }, want, Change::Structure);

  expect_refused("no such node", instrument(), [](Patch& p) { remove_node(p, "nope"); },
                 "'nope' names no node: there is nothing to remove");
}

void run_connect_tests() {
  Patch want = instrument();
  at(want, "env").values.erase("maxValue");
  at(want, "env").wires["maxValue"] = {Wire{"lfo"}};
  expect_applies("a pin's number replaced by a feed", instrument(),
                 [](Patch& p) { return connect(p, "env", "maxValue", "lfo", false); }, want, Change::Structure);

  want = instrument();
  at(want, "osc").wires["amplitude"] = {Wire{"lfo"}};
  expect_applies("a single feed replaced", instrument(),
                 [](Patch& p) { return connect(p, "osc", "amplitude", "lfo", false); }, want, Change::Structure);

  Patch listed = instrument();
  listed.graph.nodes.push_back(node("l", "MultiSource"));
  want = listed;
  at(want, "l").wires["source"] = {Wire{"lfo"}, Wire{"lfo"}};
  expect_applies("a list takes one more, the same source twice", listed, [](Patch& p) {
    connect(p, "l", "source", "lfo", false);
    return connect(p, "l", "source", "lfo", false);
  }, want, Change::Structure);

  want = instrument();
  at(want, "wt").wires["evolution"] = {Wire{"pe"}};
  expect_applies("a slot given a type its door takes", instrument(),
                 [](Patch& p) { return connect(p, "wt", "evolution", "pe", false); }, want, Change::Structure);

  want = instrument();
  at(want, "env").values.erase("maxValue");
  at(want, "env").wires["maxValue"] = {Wire{"osc", true}};
  expect_applies("a loop through a tap", instrument(),
                 [](Patch& p) { return connect(p, "env", "maxValue", "osc", true); }, want, Change::Structure);

  expect_refused("not a pin", instrument(), [](Patch& p) { connect(p, "osc", "nope", "lfo", false); },
                 "node 'osc' (SineSource): 'nope' is not a pin of SineSource");
  expect_refused("a source that names no node", instrument(), [](Patch& p) { connect(p, "osc", "amplitude", "nope", false); },
                 "'amplitude' reads 'nope', which names no node");
  expect_refused("a part into a signal pin", instrument(), [](Patch& p) { connect(p, "osc", "amplitude", "ep", false); },
                 "'amplitude' does not accept a ExplicitPartials, a part, not a signal");
  expect_refused("a slot given a type its door does not take", instrument(),
                 [](Patch& p) { connect(p, "wt", "evolution", "ep", false); }, "'evolution' does not accept a ExplicitPartials");
  expect_refused("a slot through a tap", instrument(), [](Patch& p) { connect(p, "wt", "evolution", "pe", true); },
                 "'evolution' is a slot: it takes a part by ref, not through a tap");
  expect_refused("a loop with no tap", instrument(), [](Patch& p) { connect(p, "env", "maxValue", "osc", false); },
                 "a cycle with no tap in it");
  expect_refused("a node feeding itself", instrument(), [](Patch& p) { connect(p, "lfo", "frequency", "lfo", false); },
                 "a cycle with no tap in it: lfo -> lfo");
}

void run_disconnect_tests() {
  Patch want = instrument();
  at(want, "osc").wires.erase("amplitude");
  at(want, "osc").values["amplitude"] = default_of("SineSource", "amplitude");
  expect_applies("a Param's last feed", instrument(), [](Patch& p) { return disconnect(p, "osc", "amplitude", 0); },
                 want, Change::Structure);

  Patch listed = instrument();
  listed.graph.nodes.push_back(node("l", "MultiSource"));
  at(listed, "l").wires["source"] = {Wire{"lfo"}, Wire{"env"}};
  want = listed;
  at(want, "l").wires["source"] = {Wire{"env"}};
  expect_applies("one feed of a list", listed, [](Patch& p) { return disconnect(p, "l", "source", 0); }, want,
                 Change::Structure);

  expect_refused("a feed number out of range", instrument(), [](Patch& p) { disconnect(p, "osc", "amplitude", 1); },
                 "node 'osc' (SineSource): 'amplitude' has no feed number 1; it has 1");
  expect_refused("a pin with no feed", instrument(), [](Patch& p) { disconnect(p, "env", "maxValue", 0); },
                 "'maxValue' has no feed number 0; it has 0");
  expect_refused("not a pin", instrument(), [](Patch& p) { disconnect(p, "osc", "nope", 0); },
                 "'nope' is not a pin of SineSource");
}

void run_dynamic_pin_and_output_tests() {
  Patch want = instrument();
  at(want, "env").dynamicPins["timeScale"] = "lfo";
  expect_applies("a dynamic pin's source replaced", instrument(),
                 [](Patch& p) { return feed_setting(p, "env", "timeScale", "lfo"); }, want, Change::Structure);
  want = instrument();
  at(want, "env").dynamicPins.clear();
  expect_applies("a dynamic pin removed", instrument(), [](Patch& p) { return unfeed_setting(p, "env", "timeScale"); },
                 want, Change::Structure);

  const auto structural = a_structural_setting();
  REQUIRE(structural.has_value());
  Patch withStructural = instrument();
  withStructural.graph.nodes.push_back(node("st", structural->first.c_str()));
  REQUIRE(withStructural.check().empty());
  expect_refused("a structural setting", withStructural,
                 [&](Patch& p) { feed_setting(p, "st", structural->second, "lfo"); }, "is a structural setting on");
  expect_refused("not a setting", instrument(), [](Patch& p) { feed_setting(p, "env", "maxValue", "lfo"); },
                 "node 'env' (Envelope): 'maxValue' is not a setting of Envelope");
  expect_refused("a part as the source", instrument(), [](Patch& p) { feed_setting(p, "env", "timeScale", "ep"); },
                 "'timeScale' is a ExplicitPartials, a part, not a signal");
  expect_refused("a source that names no node", instrument(), [](Patch& p) { feed_setting(p, "env", "timeScale", "nope"); },
                 "'timeScale' reads 'nope', which names no node");
  expect_refused("a loop with no tap", instrument(), [](Patch& p) { feed_setting(p, "env", "timeScale", "osc"); },
                 "a cycle with no tap in it");
  expect_refused("a dynamic pin on a plain sound", plain_sound(),
                 [](Patch& p) { feed_setting(p, "env", "timeScale", "lfo"); },
                 "a plain sound has no note, so nothing drives a dynamic pin");
  expect_refused("no dynamic pin to remove", instrument(), [](Patch& p) { unfeed_setting(p, "env", "timeMode"); },
                 "node 'env' (Envelope): 'timeMode' has no dynamic pin to remove");

  want = instrument();
  want.graph.output = "lfo";
  expect_applies("another output", instrument(), [](Patch& p) { return set_output(p, "lfo"); }, want, Change::Structure);
  want.graph.output.clear();
  expect_applies("no output", instrument(), [](Patch& p) { return set_output(p, ""); }, want, Change::Structure);
  expect_refused("an output that names no node", instrument(), [](Patch& p) { set_output(p, "nope"); },
                 "'nope' names no node");
  expect_refused("a part as the output", instrument(), [](Patch& p) { set_output(p, "ep"); },
                 "'ep' is a ExplicitPartials, a part, not a signal");
}

void run_rename_node_tests() {
  Patch grouped = instrument();
  grouped.layout.groups = {Group{"G", {"freq"}, Vec2{}}};
  Patch want = grouped;
  at(want, "freq").id = "pitch";
  at(want, "osc").wires["frequency"] = {Wire{"pitch"}};
  at(want, "env").dynamicPins["timeScale"] = "pitch";
  want.layout.positions.erase("freq");
  want.layout.positions["pitch"] = Vec2{0, 0};
  want.layout.groups[0].members = {"pitch"};
  expect_applies("every place that named it", grouped, [](Patch& p) { return rename_node(p, "freq", "pitch"); }, want,
                 Change::Structure);
  want = instrument();
  at(want, "osc").id = "out";
  want.graph.output = "out";
  want.layout.positions.erase("osc");
  want.layout.positions["out"] = Vec2{200, 50};
  expect_applies("the output", instrument(), [](Patch& p) { return rename_node(p, "osc", "out"); }, want,
                 Change::Structure);
  expect_applies("its own id", instrument(), [](Patch& p) { return rename_node(p, "osc", "osc"); }, instrument(),
                 Change::Structure);
  expect_applies("an id with a dot and a leading __", instrument(), [](Patch& p) { return rename_node(p, "lfo", "__a.b"); },
                 [] { Patch w = instrument(); at(w, "lfo").id = "__a.b"; return w; }(), Change::Structure);
  expect_refused("an empty id", instrument(), [](Patch& p) { rename_node(p, "osc", ""); }, "an id is empty");
  expect_refused("a taken id", instrument(), [](Patch& p) { rename_node(p, "osc", "env"); }, "'env' names two things");
  expect_refused("no such node", instrument(), [](Patch& p) { rename_node(p, "nope", "x"); },
                 "'nope' names no node: there is nothing to rename");
}

void run_arrays_and_instrument_tests() {
  Patch curved = instrument();
  REQUIRE(add_node(curved, "CurveNode", "c", 1, Vec2{}) == Change::Structure);
  Patch want = curved;
  at(want, "c").arrays["x"] = {0.0f, 0.5f, 1.0f};
  at(want, "c").arrays["y"] = {0.0f, 0.2f, 1.0f};
  expect_applies("a table's columns at once", curved,
                 [](Patch& p) { return set_arrays(p, "c", {{"x", {0.0f, 0.5f, 1.0f}}, {"y", {0.0f, 0.2f, 1.0f}}}); },
                 want, Change::Structure);
  expect_refused("a ragged table", curved, [](Patch& p) { set_arrays(p, "c", {{"x", {0.0f, 0.5f, 1.0f}}}); },
                 "table 'knots' takes its columns at one length");
  expect_refused("not an array", curved, [](Patch& p) { set_arrays(p, "c", {{"nope", {1.0f}}}); },
                 "node 'c' (CurveNode): 'nope' is not an array of CurveNode");
  expect_refused("a row not finite", curved, [](Patch& p) { set_arrays(p, "c", {{"x", {0.0f, kInf}}, {"y", {0.0f, 1.0f}}}); },
                 "node 'c' (CurveNode): 'x' row 1 is inf: an array's row is a finite number");
  Patch env = instrument();
  REQUIRE(add_node(env, "Envelope", "e", 1, Vec2{}) == Change::Structure);
  std::map<std::string, std::vector<float>> stages = at(env, "e").arrays;
  stages["type"][0] = 99.0f;
  expect_refused("a labelled column's row outside its labels", env, [&](Patch& p) { set_arrays(p, "e", stages); },
                 "'type' row 0 is 99, which is not an index into its labels");

  InstrumentBlock block;
  block.polyphony = 0;
  block.volume = 3.0f;
  block.sustaining = true;
  block.onsets = {"tongue", "slur"};
  block.glideMs = 0.0f;
  want = plain_sound();
  want.instrument = block;
  expect_applies("a plain sound becomes an instrument", plain_sound(),
                 [&](Patch& p) { return set_instrument(p, block); }, want, Change::Structure);
  block.volume = kInf;
  expect_refused("a volume not finite", instrument(), [&](Patch& p) { set_instrument(p, block); },
                 "the instrument's volume is inf: a volume is a finite number");
  block.volume = 1.0f;
  block.glideMs = -kInf;
  expect_refused("a glide not finite", instrument(), [&](Patch& p) { set_instrument(p, block); },
                 "the instrument's glideMs is -inf: a glide time is a finite number");
}
