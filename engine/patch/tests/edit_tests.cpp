// The edits of patch/edit.h on a small instrument made here: each applies
// and moves nothing else, each refusal throws its sentence and leaves the
// patch whole, an accepted edit keeps check() empty, and an edited patch
// survives the codec.
#include "mforce/testing/check.h"
#include "mforce/patch/edit.h"
#include "mforce/patch/instrument_block.h"
#include "mforce/patch/patch.h"
#include "mforce/patch/patch_json.h"
#include "mforce/patch/type_declarations.h"
#include <cstdio>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mforce;

namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();

Node node(const char* id, const char* type) {
  Node n;
  n.id = id;
  n.type = type;
  return n;
}

// An instrument check() passes: a note's frequency and an Envelope into a
// sine (the Envelope's timeScale pushed per note), and three nodes standing
// by for the edits: partials with an int and a bool setting, a name gate with
// a string setting, a wavetable with a slot.
Patch instrument() {
  Patch p;
  Node freq = node("freq", "PerformNode");
  freq.settings["field"] = 0.0f;
  Node env = node("env", "Envelope");
  env.values["maxValue"] = 0.8f;
  env.settings["timeMode"] = 1.0f;
  env.dynamicPins["timeScale"] = "freq";
  Node osc = node("osc", "SineSource");
  osc.wires["frequency"] = {Wire{"freq"}};
  osc.wires["amplitude"] = {Wire{"env"}};
  p.graph.nodes = {freq, env, osc, node("ep", "ExplicitPartials"), node("g", "NameGate"),
                   node("wt", "WavetableSource")};
  p.graph.output = "osc";
  p.instrument = InstrumentBlock{};
  p.layout.positions = {{"freq", Vec2{0, 0}}, {"env", Vec2{0, 100}}, {"osc", Vec2{200, 50}}};
  return p;
}

Node& at(Patch& p, const char* id) {
  for (auto& n : p.graph.nodes)
    if (n.id == id) return n;
  throw std::runtime_error(std::string("no node '") + id + "' in the case's patch");
}

// One accepted edit: what it does to the patch, the field it should have
// assigned (applied to a copy by hand), and the kind it returns.
struct Apply {
  const char* name;
  std::function<Change(Patch&)> edit;
  std::function<void(Patch&)> expected;
  Change kind;
};

void check_applies(const Apply& a) {
  Patch p = instrument();
  Patch want = p;
  a.expected(want);
  const Change kind = a.edit(p);
  if (!(p == want)) std::printf("  %s: the patch is not the copy with the one field assigned\n", a.name);
  CHECK(p == want);
  CHECK(kind == a.kind);
  const auto lines = p.check();
  for (const auto& l : lines) std::printf("  %s: check() says %s\n", a.name, l.c_str());
  CHECK(lines.empty());
}

// One refusal: a change made to the fixture first (empty for none), the
// edit, and the whole sentence it throws.
struct Refuse {
  const char* name;
  std::function<void(Patch&)> setup;
  std::function<Change(Patch&)> edit;
  std::string sentence;
};

void check_refuses(const Refuse& r) {
  Patch p = instrument();
  if (r.setup) r.setup(p);
  const Patch before = p;
  std::string got = "(nothing was thrown)";
  try {
    r.edit(p);
  } catch (const std::runtime_error& e) {
    got = e.what();
  }
  if (got != r.sentence) std::printf("  %s:\n    got  %s\n    want %s\n", r.name, got.c_str(), r.sentence.c_str());
  CHECK(got == r.sentence);
  CHECK(p == before);
}

// A list pin that is not a slot, of some registered type: (type, pin).
std::optional<std::pair<std::string, std::string>> a_list_pin() {
  for (const auto& type : declared_types())
    for (const auto& in : type_declarations(type)->inputs)
      if (in.list && !in.slot) return std::make_pair(type, in.name);
  return std::nullopt;
}

std::vector<Apply> value_and_setting_applies() {
  return {
    {"a pin's constant", [](Patch& p) { return set_value(p, "env", "maxValue", 0.5f); },
     [](Patch& p) { at(p, "env").values["maxValue"] = 0.5f; }, Change::Value},
    {"a pin holding nothing", [](Patch& p) { return set_value(p, "env", "minValue", 0.1f); },
     [](Patch& p) { at(p, "env").values["minValue"] = 0.1f; }, Change::Value},
    {"a constant outside its advisory range", [](Patch& p) { return set_value(p, "env", "maxValue", 1e6f); },
     [](Patch& p) { at(p, "env").values["maxValue"] = 1e6f; }, Change::Value},
    {"the same constant again", [](Patch& p) { return set_value(p, "env", "maxValue", 0.8f); },
     [](Patch&) {}, Change::Value},
    {"a float setting with a dynamic pin", [](Patch& p) { return set_setting(p, "env", "timeScale", 2.0f); },
     [](Patch& p) { at(p, "env").settings["timeScale"] = 2.0f; }, Change::Structure},
    {"an enum setting", [](Patch& p) { return set_setting(p, "env", "timeMode", 0.0f); },
     [](Patch& p) { at(p, "env").settings["timeMode"] = 0.0f; }, Change::Structure},
    {"an int setting", [](Patch& p) { return set_setting(p, "ep", "maxPartials", 50.0f); },
     [](Patch& p) { at(p, "ep").settings["maxPartials"] = 50.0f; }, Change::Structure},
    {"a bool setting", [](Patch& p) { return set_setting(p, "ep", "evolve", 1.0f); },
     [](Patch& p) { at(p, "ep").settings["evolve"] = 1.0f; }, Change::Structure},
    {"a string setting", [](Patch& p) { return set_string(p, "g", "name", "anything at all"); },
     [](Patch& p) { at(p, "g").strings["name"] = "anything at all"; }, Change::Structure},
  };
}

std::vector<Apply> layout_applies() {
  return {
    {"a node's position", [](Patch& p) { return move_node(p, "osc", Vec2{5, 6}); },
     [](Patch& p) { p.layout.positions["osc"] = Vec2{5, 6}; }, Change::Layout},
    {"a first position", [](Patch& p) { return move_node(p, "ep", Vec2{-3, 4}); },
     [](Patch& p) { p.layout.positions["ep"] = Vec2{-3, 4}; }, Change::Layout},
    {"the Output node's position", [](Patch& p) { return move_output(p, Vec2{400, 50}); },
     [](Patch& p) { p.layout.outputPos = Vec2{400, 50}; }, Change::Layout},
    {"the panning", [](Patch& p) { return set_panning(p, Vec2{-20, 30}); },
     [](Patch& p) { p.layout.panning = Vec2{-20, 30}; }, Change::Layout},
  };
}

}  // namespace

void run_edit_apply_tests() {
  REQUIRE(instrument().check().empty());
  for (const auto& a : value_and_setting_applies()) check_applies(a);
  for (const auto& a : layout_applies()) check_applies(a);
}

void run_edit_value_refusal_tests() {
  const auto unknown = [](Patch& p) { p.graph.nodes.push_back(node("x", "NoSuchSource")); };
  const std::vector<Refuse> cases = {
    {"no such node", {}, [](Patch& p) { return set_value(p, "nope", "maxValue", 1.0f); },
     "'nope' names no node: it has no 'maxValue'"},
    {"an unregistered type", unknown, [](Patch& p) { return set_value(p, "x", "gain", 1.0f); },
     "node 'x' (NoSuchSource): Unknown node type: NoSuchSource; so 'gain' has no declaration to check it against"},
    {"not a pin", {}, [](Patch& p) { return set_value(p, "env", "timeScale", 1.0f); },
     "node 'env' (Envelope): 'timeScale' is not a pin of Envelope"},
    {"a slot", {}, [](Patch& p) { return set_value(p, "wt", "evolution", 1.0f); },
     "node 'wt' (WavetableSource): 'evolution' is a slot: it takes a part, not a number"},
    {"a fed list", [](Patch& p) { Node n = node("l", "MultiSource"); n.wires["source"] = {Wire{"freq"}}; p.graph.nodes.push_back(n); },
     [](Patch& p) { return set_value(p, "l", "source", 0.5f); },
     "node 'l' (MultiSource): 'source' is fed by 'freq': a pin takes a number or a feed, not both"},
    {"a fed pin", {}, [](Patch& p) { return set_value(p, "osc", "frequency", 440.0f); },
     "node 'osc' (SineSource): 'frequency' is fed by 'freq': a pin takes a number or a feed, not both"},
    {"not finite", {}, [](Patch& p) { return set_value(p, "env", "maxValue", kInf); },
     "node 'env' (Envelope): 'maxValue' is inf: a pin's constant is a finite number"},
  };
  for (const auto& r : cases) check_refuses(r);
}

void run_edit_setting_refusal_tests() {
  const auto unknown = [](Patch& p) { p.graph.nodes.push_back(node("x", "NoSuchSource")); };
  const std::vector<Refuse> cases = {
    {"no such node", {}, [](Patch& p) { return set_setting(p, "nope", "timeMode", 0.0f); },
     "'nope' names no node: it has no 'timeMode'"},
    {"an unregistered type", unknown, [](Patch& p) { return set_setting(p, "x", "mode", 0.0f); },
     "node 'x' (NoSuchSource): Unknown node type: NoSuchSource; so 'mode' has no declaration to check it against"},
    {"a pin, not a setting", {}, [](Patch& p) { return set_setting(p, "env", "maxValue", 1.0f); },
     "node 'env' (Envelope): 'maxValue' is not a setting of Envelope"},
    {"a string setting", {}, [](Patch& p) { return set_setting(p, "g", "name", 1.0f); },
     "node 'g' (NameGate): 'name' is not a setting of NameGate"},
    {"a bool not 0 or 1", {}, [](Patch& p) { return set_setting(p, "ep", "evolve", 2.0f); },
     "node 'ep' (ExplicitPartials): 'evolve' is 2: a bool setting holds 0 or 1"},
    {"an enum outside its labels", {}, [](Patch& p) { return set_setting(p, "env", "timeMode", 2.0f); },
     "node 'env' (Envelope): 'timeMode' is 2, which is not an index into its labels (fraction, seconds)"},
    {"a float above its range", {}, [](Patch& p) { return set_setting(p, "env", "timeScale", 20.0f); },
     "node 'env' (Envelope): 'timeScale' is 20, outside its range 0.01 to 10"},
    {"an int above its range", {}, [](Patch& p) { return set_setting(p, "ep", "maxPartials", 201.0f); },
     "node 'ep' (ExplicitPartials): 'maxPartials' is 201, outside its range 1 to 200"},
    {"an int not whole", {}, [](Patch& p) { return set_setting(p, "ep", "maxPartials", 2.5f); },
     "node 'ep' (ExplicitPartials): 'maxPartials' is 2.5: an int setting holds a whole number"},
    {"not finite", {}, [](Patch& p) { return set_setting(p, "env", "timeScale", -kInf); },
     "node 'env' (Envelope): 'timeScale' is -inf: a setting holds a finite number"},
  };
  for (const auto& r : cases) check_refuses(r);
}

void run_edit_string_and_layout_refusal_tests() {
  const auto unknown = [](Patch& p) { p.graph.nodes.push_back(node("x", "NoSuchSource")); };
  const std::vector<Refuse> cases = {
    {"no such node", {}, [](Patch& p) { return set_string(p, "nope", "name", "a"); },
     "'nope' names no node: it has no 'name'"},
    {"an unregistered type", unknown, [](Patch& p) { return set_string(p, "x", "name", "a"); },
     "node 'x' (NoSuchSource): Unknown node type: NoSuchSource; so 'name' has no declaration to check it against"},
    {"not a string setting", {}, [](Patch& p) { return set_string(p, "env", "timeScale", "fast"); },
     "node 'env' (Envelope): 'timeScale' is not a string setting of Envelope"},
    {"a move of no node", {}, [](Patch& p) { return move_node(p, "nope", Vec2{1, 2}); },
     "'nope' names no node: there is nothing to move"},
    {"a node moved off the plane", {}, [](Patch& p) { return move_node(p, "osc", Vec2{kInf, 2}); },
     "node 'osc' (SineSource): its position (inf, 2) is not two finite numbers"},
    {"the Output node moved off the plane", {}, [](Patch& p) { return move_output(p, Vec2{0, -kInf}); },
     "the Output node's position (0, -inf) is not two finite numbers"},
    {"the panning off the plane", {}, [](Patch& p) { return set_panning(p, Vec2{kInf, kInf}); },
     "the canvas's panning (inf, inf) is not two finite numbers"},
  };
  for (const auto& r : cases) check_refuses(r);
}

namespace {

// `p` written with save_patch and read back with load_patch.
Patch through_the_codec(const Patch& p) {
  const std::filesystem::path file = std::filesystem::temp_directory_path() / "mforce_patch_edit_codec_test.json";
  save_patch(file.string(), p);
  Patch back = load_patch(file.string());
  std::error_code ignored;
  std::filesystem::remove(file, ignored);
  return back;
}

}  // namespace

// A number on a list pin with no feed is taken, as the codec, check() and
// the build take one; the patch stays whole and survives the codec.
void run_edit_list_pin_tests() {
  const auto list = a_list_pin();
  REQUIRE(list.has_value());
  Patch p = instrument();
  p.graph.nodes.push_back(node("l", list->first.c_str()));
  REQUIRE(p.check().empty());
  Patch want = p;
  at(want, "l").values[list->second] = 0.25f;
  CHECK(set_value(p, "l", list->second, 0.25f) == Change::Value);
  CHECK(p == want);
  CHECK(p.check().empty());
}

void run_edit_codec_tests() {
  // Read once first: a record made in code holds only what it was given, and
  // the reader fills every name a node's type declares with its default.
  Patch p = through_the_codec(instrument());
  set_value(p, "env", "maxValue", 0.25f);
  set_setting(p, "ep", "maxPartials", 12.0f);
  set_string(p, "g", "name", "edited");
  move_node(p, "ep", Vec2{7.5f, -2.0f});
  move_output(p, Vec2{300, 10});
  set_panning(p, Vec2{-1.5f, 2.5f});
  const auto list = a_list_pin();
  REQUIRE(list.has_value());
  p.graph.nodes.push_back(node("l", list->first.c_str()));
  p = through_the_codec(p);
  set_value(p, "l", list->second, 0.75f);                // a number on a list pin
  CHECK(through_the_codec(p) == p);
}
