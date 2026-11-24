#pragma once
// The small instrument the structural edit tests start from, and the way
// they check a refusal: the sentence holds a given part and the patch is
// left as it was.
#include "mforce/testing/check.h"
#include "mforce/patch/edit.h"
#include "mforce/patch/instrument_block.h"
#include "mforce/patch/patch.h"
#include "mforce/patch/type_declarations.h"
#include <cstdio>
#include <exception>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace edit_fixture {

using namespace mforce;

inline Node node(const char* id, const char* type) {
  Node n;
  n.id = id;
  n.type = type;
  return n;
}

// An instrument check() passes: a note's frequency (freq) and an Envelope
// (env) into a sine (osc, the output), the Envelope's timeScale pushed per
// note by freq; standing by, partials (ep, a part), a wavetable with a slot
// (wt), a pluck evolution that slot takes (pe) and a second sine (lfo).
inline Patch instrument() {
  Patch p;
  Node freq = node("freq", "PerformNode");
  freq.settings["field"] = 0.0f;
  Node env = node("env", "Envelope");
  env.values["maxValue"] = 0.8f;
  env.dynamicPins["timeScale"] = "freq";
  Node osc = node("osc", "SineSource");
  osc.wires["frequency"] = {Wire{"freq"}};
  osc.wires["amplitude"] = {Wire{"env"}};
  p.graph.nodes = {freq, env, osc, node("ep", "ExplicitPartials"), node("wt", "WavetableSource"),
                   node("pe", "PluckEvolution"), node("lfo", "SineSource")};
  p.graph.output = "osc";
  p.instrument = InstrumentBlock{};
  p.layout.positions = {{"freq", Vec2{0, 0}}, {"env", Vec2{0, 100}}, {"osc", Vec2{200, 50}}};
  return p;
}

inline Node& at(Patch& p, const char* id) {
  for (auto& n : p.graph.nodes)
    if (n.id == id) return n;
  throw std::runtime_error(std::string("no node '") + id + "' in the case's patch");
}

inline void erase_node(Patch& p, const char* id) {
  std::erase_if(p.graph.nodes, [&](const Node& n) { return n.id == id; });
}

// The declared default of Param `pin` on `type`.
inline float default_of(const char* type, const char* pin) {
  const auto* d = type_declarations(type);
  const auto* param = d ? d->param(pin) : nullptr;
  if (!param) throw std::runtime_error(std::string(type) + " declares no Param '" + pin + "'");
  return param->defaultValue;
}

// `edit` applied to `p`: the patch must become `want` and check() stay empty.
inline void expect_applies(const char* name, Patch p, const std::function<Change(Patch&)>& edit, const Patch& want,
                           Change kind) {
  Change got = Change::Value;
  try {
    got = edit(p);
  } catch (const std::exception& e) {
    std::printf("  %s: refused: %s\n", name, e.what());
    CHECK(false);
    return;
  }
  if (!(p == want)) std::printf("  %s: the patch is not the one expected\n", name);
  CHECK(p == want);
  CHECK(got == kind);
  for (const auto& l : p.check()) std::printf("  %s: check() says %s\n", name, l.c_str());
  CHECK(p.check().empty());
}

// `edit` refused on `p`: the sentence holds `part` and `p` is unchanged.
inline void expect_refused(const char* name, Patch p, const std::function<void(Patch&)>& edit,
                           const std::string& part) {
  const Patch before = p;
  std::string got = "(nothing was thrown)";
  try {
    edit(p);
  } catch (const std::runtime_error& e) {
    got = e.what();
  }
  if (got.find(part) == std::string::npos) std::printf("  %s:\n    got  %s\n    want ...%s...\n", name, got.c_str(), part.c_str());
  CHECK(got.find(part) != std::string::npos);
  if (!(p == before)) std::printf("  %s: the refused edit changed the patch\n", name);
  CHECK(p == before);
}

// A setting of some registered type that is structural: (type, setting).
inline std::optional<std::pair<std::string, std::string>> a_structural_setting() {
  for (const auto& type : declared_types())
    for (const auto& s : type_declarations(type)->settings)
      if (s.structural) return std::make_pair(type, s.name);
  return std::nullopt;
}

}  // namespace edit_fixture
