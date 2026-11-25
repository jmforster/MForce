// The selection of several, the level the canvas shows, and grouping: each
// refusal by its words, each change one undo step.
#include "mforce/testing/check.h"
#include "control_fixture.h"
#include "mforce/ui_control/building.h"
#include "mforce/ui_control/edits.h"
#include "mforce/ui_control/files.h"
#include "mforce/ui_control/grouping.h"
#include "mforce/ui_control/listening.h"
#include "mforce/ui_control/selecting.h"
#include "mforce/ui_control/wiring.h"
#include "mforce/ui_model/canvas_view.h"
#include "mforce/ui_model/open_patch.h"
#include <cstdio>
#include <string>
#include <vector>

using namespace control_fixture;

namespace {

using Names = std::vector<std::string>;
using Kind = PinEnd::Kind;

PinEnd end(Kind k, const char* node = "", const char* name = "") { return PinEnd{k, node, name}; }

const Group* group_of(const Session& s, const std::string& name) {
  for (const auto& g : s.open.patch.layout.groups)
    if (g.name == name) return &g;
  return nullptr;
}

// The control patch with env and osc in group G at the top.
void start_grouped(Session& s) {
  start(s, control_patch());
  select_nodes(s, {"env", "osc"});
  group_selection(s);
  REQUIRE(group_of(s, "Group") != nullptr);
}

void run_selection() {
  Session s;
  start(s, control_patch());
  select_node(s, "env");
  CHECK(s.selections == (Names{"env"}) && selected_one(s) == "env");
  select_nodes(s, {"env", "lfo", "env", ""});
  CHECK(s.selections == (Names{"env", "lfo"}) && selected_one(s).empty());
  toggle_selected(s, "env");
  toggle_selected(s, "Note1");
  CHECK(s.selections == (Names{"lfo", "Note1"}));
  rename(s, "lfo", "wobble");
  CHECK(s.selections == (Names{"wobble", "Note1"}));
  const Patch before = s.open.patch;
  delete_these(s, {}, Names(s.selections));
  CHECK(s.selections.empty() && !node_of(s, "wobble") && s.open.patch.layout.faces.empty());
  undo(s);
  CHECK(s.open.patch == before && s.selections.empty());   // an undo brings the things back, not the selection
  create_node(s, "SineSource", Vec2{});
  CHECK(s.selections == (Names{"Sine"}));
  undo(s);
  CHECK(s.selections.empty());                               // what went is no longer selected
  select_node(s, "");
  CHECK(s.selections.empty());
}

void run_group_selection() {
  Session s;
  start(s, control_patch());
  group_selection(s);
  CHECK(s.status == "Group refused: nothing selected");
  select_nodes(s, {"nope", "__perf_frequency"});
  group_selection(s);
  CHECK(s.status == "Group refused: nothing groupable selected");
  wire(s, end(Kind::Output, "lfo"), end(Kind::Input, "l", "source"));
  select_nodes(s, {"env", "lfo"});
  const size_t before = steps(s);
  group_selection(s);
  CHECK(s.status == "Group refused: selection has 2 outputs: env lfo" && steps(s) == before);
  select_nodes(s, {"env", "osc", "Note1"});
  group_selection(s);
  CHECK(s.status == "Grouped as 'Group' — double-click to open, rename in Properties" && steps(s) == before + 1);
  const Group* g = group_of(s, "Group");
  REQUIRE(g != nullptr);
  CHECK(g->members == (Names{"env", "osc", "Note1"}) && g->pos == (Vec2{-200, 200.0f / 3.0f}));
  CHECK(s.selections == (Names{"Group"}));
  undo(s);
  CHECK(s.open.patch.layout.groups.empty());
}

void run_group_inside() {
  Session s;
  start_grouped(s);
  open_group(s, "Group");
  CHECK(s.groupPath == (Names{"Group"}) && s.selections.empty());
  select_node(s, "env");
  group_selection(s);
  CHECK(group_of(s, "Group")->members == (Names{"osc", "Group2"}) && group_of(s, "Group2")->members == (Names{"env"}));
  create_node(s, "SineSource", Vec2{});
  CHECK(group_of(s, "Group")->members == (Names{"osc", "Group2", "Sine"}));   // a new node joins the open group
  create_note(s, Vec2{});
  CHECK(group_of(s, "Group")->members.back() == "Note2");
  open_group(s, "lfo");
  CHECK(s.status == "'lfo' is not a group shown here" && s.groupPath == (Names{"Group"}));
}

void run_add_remove_ungroup() {
  Session s;
  start_grouped(s);
  select_node(s, "lfo");
  add_to_group(s, "Group");
  CHECK(s.status == "1 node(s) -> 'Group'" && group_of(s, "Group")->members == (Names{"env", "osc", "lfo"}));
  CHECK(s.selections.empty());                                // lfo is not shown here any more
  open_group(s, "Group");
  select_node(s, "lfo");
  remove_from_group(s);
  CHECK(s.status == "removed from group" && group_of(s, "Group")->members == (Names{"env", "osc"}));
  go_up(s, 0);
  wire(s, end(Kind::Output, "lfo"), end(Kind::Input, "l", "source"));
  select_node(s, "lfo");
  const size_t before = steps(s);
  add_to_group(s, "Group");
  CHECK(s.status == "Add refused: 'Group' would have 2 outputs: osc lfo" && steps(s) == before);
  select_node(s, "Group");
  add_to_group(s, "Group");
  CHECK(s.status == "Add refused: nothing groupable selected");
  ungroup(s, "Group");
  CHECK(s.open.patch.layout.groups.empty() && steps(s) == before + 1);
  undo(s);
  CHECK(group_of(s, "Group") != nullptr);

  open_group(s, "Group");
  select_nodes(s, {"env", "osc"});
  remove_from_group(s);                                       // the last members leave: the group goes
  CHECK(s.open.patch.layout.groups.empty());
}

void run_rename_and_delete_group() {
  Session s;
  start_grouped(s);
  open_group(s, "Group");
  rename(s, "Group", "Voice");
  CHECK(group_of(s, "Voice") && s.groupPath == (Names{"Voice"}));
  rename(s, "Voice", "osc");
  CHECK(s.status == "name already in use by a node: osc");
  rename(s, "Voice", "Note1");
  CHECK(s.status == "name already in use by a node: Note1");
  rename(s, "Voice", "a.b");
  CHECK(s.status == "'.' not allowed");
  go_up(s, 0);
  select_node(s, "lfo");
  group_selection(s);
  rename(s, "Voice", "Group");
  CHECK(s.status == "name already in use: Group");

  const Patch before = s.open.patch;
  delete_these(s, {}, {"Voice"});
  CHECK(s.status.rfind("Deleted 2 node(s)", 0) == 0 && !group_of(s, "Voice") && !node_of(s, "env") && !node_of(s, "osc"));
  undo(s);
  CHECK(s.open.patch == before);
}


// In and up: the panning of each level, Listen ending on every change but
// going deeper, and the path cut by what removes its groups.
void run_paths() {
  Session s;
  start_grouped(s);
  pan_canvas(s, Vec2{11, 12});
  listen_at(s, "osc");
  REQUIRE(s.listening == "osc");
  open_group(s, "Group");
  CHECK(s.listening == "osc" && s.open.patch.layout.panning == (Vec2{11, 12}));
  CHECK(shown_panning(s) == (Vec2{80.0f + 300.0f, 80.0f}));   // the level framed: env at -300, osc at 0
  pan_canvas(s, Vec2{1, 2});
  CHECK(s.levelPanning == (Vec2{1, 2}) && s.open.patch.layout.panning == (Vec2{11, 12}));
  select_node(s, "env");
  group_selection(s);
  open_group(s, "Group2");
  pan_canvas(s, Vec2{3, 4});
  go_up(s, 1);
  CHECK(s.groupPath == (Names{"Group"}) && shown_panning(s) == (Vec2{1, 2}) && s.listening.empty());
  go_up(s, 0);
  CHECK(s.groupPath.empty() && shown_panning(s) == (Vec2{11, 12}));

  listen_at_group(s, "Group");
  CHECK(s.listening == "osc");
  open_group(s, "Group");
  open_group(s, "Group2");
  ungroup(s, "Group2");                                       // the open group goes: the path is cut
  CHECK(s.groupPath == (Names{"Group"}) && s.listening.empty());
  undo(s);                                                    // the group is back; the path is not
  CHECK(group_of(s, "Group2") && s.groupPath == (Names{"Group"}));
  open_group(s, "Group2");
  undo(s);                                                    // an undo that takes Group2 away cuts the path
  CHECK(!group_of(s, "Group2") && s.groupPath == (Names{"Group"}));
  new_file(s);
  CHECK(s.groupPath.empty() && s.pans.empty() && s.selections.empty());
}

// The groups of oboe1: two that would leave by two outputs are refused,
// two that leave by one are grouped, and one Undo takes it back.
void run_oboe1_grouping() {
  Session s;
  start(s, open_patch("patches/library/winds/oboe1.json").patch);
  const Patch before = s.open.patch;
  select_nodes(s, {"Breath", "Delay"});
  group_selection(s);
  CHECK(s.status.rfind("Group refused: selection has 2 outputs: ", 0) == 0);
  select_nodes(s, {"Breath", "Drive"});
  group_selection(s);
  CHECK(s.status.rfind("Grouped as 'Group'", 0) == 0 && group_of(s, "Group")->members == (Names{"Breath", "Drive"}));
  listen_at_group(s, "Group");
  CHECK(s.listening == "Damp_lpf");
  undo(s);
  CHECK(s.open.patch == before);
}

// Going to a thing's level from anywhere; a group moved like a node.
void run_go_to() {
  Session s;
  start_grouped(s);
  select_node(s, "env");
  group_selection(s);                                         // Group2 {env} at the top, beside Group {osc}
  edit_position(s, "Group2", Vec2{7, 8});
  CHECK(group_of(s, "Group2")->pos == (Vec2{7, 8}));
  go_to(s, "osc", Vec2{400, 300});
  CHECK(s.groupPath == (Names{"Group"}) && s.selections == (Names{"osc"}) && s.levelPanning == (Vec2{200, 150}));
  go_to(s, "Note1", std::nullopt);
  CHECK(s.groupPath.empty() && s.selections == (Names{"Note1"}));
  go_to(s, "__perf_frequency", std::nullopt);                // a face's node: its face
  CHECK(s.selections == (Names{"Note1"}));
  go_to(s, "nope", std::nullopt);
  CHECK(s.status == "'nope' names nothing to go to");
}

// The selection holds only what the level shows: after Add to group, after
// Remove from inside the group, and after an Undo that puts a selected thing
// back inside one; and Delete then takes nothing unseen.
void run_selection_follows_level() {
  Session s;
  start_grouped(s);
  select_node(s, "lfo");
  add_to_group(s, "Group");
  CHECK(s.selections.empty());
  delete_these(s, {}, s.selections);
  CHECK(control_fixture::node_of(s, "lfo") != nullptr);       // nothing deleted unseen
  open_group(s, "Group");
  select_node(s, "lfo");
  remove_from_group(s);                                       // lfo goes up a level, out of sight
  CHECK(s.selections.empty() && s.groupPath == (Names{"Group"}));
  go_up(s, 0);
  select_node(s, "lfo");
  undo(s);                                                    // lfo back inside the group
  CHECK(group_of(s, "Group")->members.back() == "lfo" && s.selections.empty());
  redo(s);                                                    // and out again: shown, but not reselected
  CHECK(s.selections.empty());
}

}  // namespace

void run_selection_tests() {
  std::printf("selecting: one, several, a toggle; a rename, a delete and an undo\n");
  run_selection();
}

void run_grouping_tests() {
  std::printf("grouping: group selection, inside a group, add, remove, ungroup, rename, delete\n");
  run_group_selection();
  run_group_inside();
  run_add_remove_ungroup();
  run_selection_follows_level();
  run_rename_and_delete_group();
  run_paths();
  run_oboe1_grouping();
  run_go_to();
}
