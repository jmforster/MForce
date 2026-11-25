// Copy, cut and paste as text, Duplicate, Replace with and a wire routed
// through wormholes: the old words, one undo step each. No clipboard is
// touched: the text is a string.
#include "mforce/testing/check.h"
#include "control_fixture.h"
#include "mforce/ui_control/building.h"
#include "mforce/ui_control/clipboard.h"
#include "mforce/ui_control/grouping.h"
#include "mforce/ui_control/listening.h"
#include "mforce/ui_control/selecting.h"
#include "mforce/ui_control/wiring.h"
#include <cstdio>
#include <string>
#include <vector>

using namespace control_fixture;

namespace {

using Names = std::vector<std::string>;
using Kind = PinEnd::Kind;

PinEnd end(Kind k, const char* node = "", const char* name = "") { return PinEnd{k, node, name}; }

Vec2 at_of(const Session& s, const std::string& id) { return s.open.patch.layout.positions.at(id); }

void run_copy_and_paste() {
  Session s;
  start(s, control_patch());
  CHECK(copy_text(s).empty() && s.status == "Copy: nothing selected");
  select_node(s, "Note1");
  CHECK(copy_text(s).empty() && s.status == "Copy: nothing copyable selected (Note/Output/Mixer/groups are v2)");
  select_nodes(s, {"env", "osc", "Note1"});
  const std::string text = copy_text(s);
  CHECK(!text.empty() && s.status == "Copied 2 nodes (uncopyable selection skipped)");
  const size_t before = steps(s);
  paste_text(s, text, std::nullopt);
  CHECK(s.status == "Pasted 2 nodes (env2, ...)" && s.selections == (Names{"env2", "osc2"}) && steps(s) == before + 1);
  CHECK(node_of(s, "osc2")->wires.at("amplitude") == (std::vector<Wire>{Wire{"env2"}}));
  CHECK(node_of(s, "osc2")->wires.at("frequency") == (std::vector<Wire>{Wire{"__perf_frequency"}}));
  CHECK(at_of(s, "env2") == (Vec2{-240, 260}) && node_of(s, "osc2")->seed == node_of(s, "osc")->seed);
  paste_text(s, text, std::nullopt);                          // the next paste lands another (60, 60) on
  CHECK(at_of(s, "env3") == (Vec2{-180, 320}));
  paste_text(s, text, Vec2{500, 500});                        // from the create menu: where it was opened
  CHECK(at_of(s, "env4") == (Vec2{500, 500}) && at_of(s, "osc4") == (Vec2{800, 300}));
  undo(s);
  CHECK(!node_of(s, "env4") && node_of(s, "env3"));

  select_nodes(s, {"env", "osc"});
  group_selection(s);
  open_group(s, "Group");
  paste_text(s, text, std::nullopt);
  const auto& members = s.open.patch.layout.groups.front().members;
  CHECK(members.size() == 4 && members[2] == "env4" && members[3] == "osc4");

  Session other;
  Patch lone;
  (void)set_instrument(lone, InstrumentBlock{});
  (void)add_node(lone, "SineSource", "lfo", 9, Vec2{});
  start(other, lone);
  paste_text(other, text, std::nullopt);
  CHECK(other.status == "Pasted 2 nodes (env...) — 1 unresolvable ref dropped");
  CHECK(node_of(other, "osc") && !node_of(other, "osc")->wires.count("frequency"));
}

void run_paste_refusals() {
  Session s;
  start(s, control_patch());
  const Patch before = s.open.patch;
  paste_text(s, "", std::nullopt);
  CHECK(s.status == "Paste: clipboard empty");
  paste_text(s, "hello", std::nullopt);
  CHECK(s.status == "Paste: clipboard is not an MForce fragment");
  paste_text(s, R"J({"mforceClip":1,"nodes":[]})J", std::nullopt);
  CHECK(s.status == "Paste: fragment had no nodes");
  paste_text(s, R"J({"mforceClip":1,"nodes":[{"id":"x","type":"NoSuchSource"}]})J", std::nullopt);
  CHECK(s.status.rfind("Paste failed: ", 0) == 0 && s.status.find("NoSuchSource") != std::string::npos);
  CHECK(s.open.patch == before && steps(s) == 0);
}

void run_cut() {
  Session s;
  start(s, control_patch());
  const Patch before = s.open.patch;
  select_nodes(s, {"lfo", "Note1"});
  const std::string text = cut_text(s);
  CHECK(!text.empty() && s.status == "Copied 1 node (uncopyable selection skipped)");
  CHECK(!node_of(s, "lfo") && !s.open.patch.layout.faces.empty() && steps(s) == 1);
  undo(s);
  CHECK(s.open.patch == before);
  paste_text(s, text, std::nullopt);
  CHECK(node_of(s, "lfo2") && s.status == "Pasted 1 node (lfo2)");

  // A cut never deletes what it did not copy: a group stays.
  select_nodes(s, {"env", "osc"});
  group_selection(s);
  REQUIRE(s.selections == (Names{"Group"}));
  const Patch grouped = s.open.patch;
  const size_t was = steps(s);
  CHECK(cut_text(s).empty());
  CHECK(s.status == "Cut: nothing copyable selected (Note/Output/Mixer/groups are v2) — Delete removes a group");
  CHECK(s.open.patch == grouped && steps(s) == was);
  select_nodes(s, {"Group", "lfo"});                          // some cut, some skipped: the copy's words
  CHECK(!cut_text(s).empty() && s.status == "Copied 1 node (uncopyable selection skipped)");
  CHECK(!node_of(s, "lfo") && node_of(s, "osc") && !s.open.patch.layout.groups.empty());
  select_nodes(s, {});
  CHECK(cut_text(s).empty() && s.status == "Cut: nothing selected");
}

void run_duplicate_and_replace() {
  Session s;
  start(s, control_patch());
  const Patch before = s.open.patch;
  duplicate(s, "osc");
  REQUIRE(node_of(s, "osc2") != nullptr);
  CHECK(at_of(s, "osc2") == (Vec2{30, 30}) && s.selections == (Names{"osc2"}) && steps(s) == 1);
  CHECK(node_of(s, "osc2")->wires == node_of(s, "osc")->wires && node_of(s, "osc2")->seed == node_of(s, "osc")->seed);
  undo(s);
  CHECK(s.open.patch == before);
  duplicate(s, "Note1");
  CHECK(s.status == "'Note1' is a Note: it is not duplicated" && steps(s) == 0);

  select_node(s, "lfo");
  listen_at(s, "lfo");
  replace_with(s, "lfo", "SawSource");
  CHECK(s.status == "Replaced lfo with Saw (0 wires kept)" && s.selections == (Names{"Saw"}) && s.listening == "Saw");
  replace_with(s, "env", "SineSource");
  CHECK(s.status == "Replaced env with Sine (1 wire kept; dropped: arrays)");
  CHECK(node_of(s, "osc")->wires.at("amplitude") == (std::vector<Wire>{Wire{"Sine"}}));
  undo(s);
  undo(s);
  CHECK(s.open.patch == before);
  replace_with(s, "Note1", "SineSource");
  CHECK(s.status == "'Note1' is a Note: it is not replaced");
}

void run_route_wire() {
  Session s;
  start(s, control_patch());
  const Patch before = s.open.patch;
  route_wire(s, end(Kind::Input, "osc", "amplitude"), 0);
  CHECK(s.status == "wormhole pair linked — span hidden; hover or select either half to see it" && steps(s) == 1);
  CHECK(node_of(s, "Wormhole")->wires.at("source") == (std::vector<Wire>{Wire{"env"}}));
  CHECK(node_of(s, "Wormhole2")->wires.at("source") == (std::vector<Wire>{Wire{"Wormhole"}}));
  CHECK(node_of(s, "osc")->wires.at("amplitude") == (std::vector<Wire>{Wire{"Wormhole2"}}));
  CHECK(at_of(s, "Wormhole") == (Vec2{-70, 230}) && at_of(s, "Wormhole2") == (Vec2{-160, 30}));
  route_wire(s, end(Kind::Input, "osc", "amplitude"), 0);
  CHECK(s.status == "the wire runs through a wormhole already: it is not routed again" && steps(s) == 1);
  undo(s);
  CHECK(s.open.patch == before);

  wire(s, end(Kind::Output, "env"), end(Kind::Input, "l", "source"));
  wire(s, end(Kind::Tap, "lfo"), end(Kind::Input, "l", "source"));
  route_wire(s, end(Kind::Input, "l", "source"), 0);          // the first of two feeds, the second kept after it
  CHECK(node_of(s, "l")->wires.at("source") == (std::vector<Wire>{Wire{"Wormhole2"}, Wire{"lfo", true}}));
  route_wire(s, end(Kind::Input, "l", "source"), 1);          // a tap stays a tap at the source
  CHECK(node_of(s, "Wormhole3")->wires.at("source") == (std::vector<Wire>{Wire{"lfo", true}}));
  route_wire(s, end(Kind::PatchOutput), 0);
  CHECK(s.open.patch.graph.output == "Wormhole6" && node_of(s, "Wormhole5")->wires.at("source")[0].source == "osc");
  route_wire(s, end(Kind::SettingPin, "env", "timeScale"), 0);
  CHECK(s.status == "a setting's wire is not routed through wormholes");

  Session hand;
  start(hand, control_patch());
  create_node(hand, "Wormhole", Vec2{});
  create_node(hand, "Wormhole", Vec2{100, 0});
  wire(hand, end(Kind::Output, "Wormhole"), end(Kind::Input, "Wormhole2", "source"));
  CHECK(hand.status == "wormhole pair linked — span hidden; hover or select either half to see it");
}

}  // namespace

void run_clipboard_tests() {
  std::printf("clipboard: copy, paste, cut as text; duplicate, replace with; a wire through wormholes\n");
  run_copy_and_paste();
  run_paste_refusals();
  run_cut();
  run_duplicate_and_replace();
  run_route_wire();
}
