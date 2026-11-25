// Groups by the mouse and the keys: Group selection from the menu, the
// group drawn as one node with its pins, going in by a double-click and up
// by the breadcrumb, a wire made and pulled off on a group's pin, Add to
// group, Remove from, Ungroup, a rename in the panel, Delete, and Listen from
// the group's menu and the breadcrumb's switch.
#include "gesture_bench.h"
#include "mforce/ui_control/grouping.h"

using namespace gesture_bench;

namespace {

// The bench with osc and env made one group, G, by the menu.
void group_osc_and_env(Bench& b) {
  b.editor.click_at(b.grip("node:osc"));
  b.editor.ctrl_click_at(b.grip("node:env"));
  b.editor.right_click_at(b.grip("node:osc"));
  REQUIRE(b.editor.click_on("menu:Group selection (2)"));
  b.editor.settle();
}

void run_group_and_levels() {
  Bench b;
  group_osc_and_env(b);
  const Group* g = group_of(b.s, "Group");
  REQUIRE(g != nullptr);
  CHECK(g->members.size() == 2 && b.s.selections == (Names{"Group"}) && holds(b.s));
  CHECK(b.drawn("group:Group") && !b.drawn("node:osc") && !b.drawn("node:env"));
  CHECK(b.drawn("pin:Group.osc.frequency") && b.drawn("pin:Group.out"));   // the wire in from Note1; osc to the Output
  CHECK(b.drawn("crumb:0") && !b.drawn("crumb:1"));
  b.editor.double_click_at(b.grip("group:Group"));
  CHECK(b.s.groupPath == (Names{"Group"}) && b.drawn("node:osc") && !b.drawn("node:lfo") && b.drawn("crumb:1"));
  CHECK(b.s.selections.empty() && holds(b.s));
  b.editor.settle();
  b.editor.right_click_at(b.empty());
  REQUIRE(b.editor.click_on("menu:Var"));
  CHECK(node_of(b.s, "Var") && group_of(b.s, "Group")->members.back() == "Var");   // made inside: in the group
  b.editor.settle();
  REQUIRE(b.editor.click_on("crumb:0"));
  CHECK(b.s.groupPath.empty() && b.drawn("node:lfo") && holds(b.s));
}

void run_group_pins() {
  Bench b;
  group_osc_and_env(b);
  const auto lfoOut = b.editor.area("pin:lfo.out"), in = b.editor.area("pin:Group.osc.frequency");
  REQUIRE(lfoOut && in);
  b.editor.drag(lfoOut->centre(), in->centre());              // onto the group's pin: the member's real pin
  CHECK(node_of(b.s, "osc")->wires.at("frequency") == (std::vector<Wire>{Wire{"lfo"}}) && holds(b.s));
  b.editor.settle();
  const auto again = b.editor.area("pin:Group.osc.frequency");
  REQUIRE(again);
  b.editor.drag(again->centre(), b.empty());                  // pulled off it
  CHECK(!node_of(b.s, "osc")->wires.count("frequency") && holds(b.s));
}

void run_add_remove_ungroup() {
  Bench b;
  group_osc_and_env(b);
  b.editor.right_click_at(b.grip("node:lfo"));
  REQUIRE(b.editor.click_on("menu:Add to group (1)"));
  REQUIRE(b.editor.click_on("menu:Group"));
  CHECK(group_of(b.s, "Group")->members.back() == "lfo" && !b.drawn("node:lfo") && holds(b.s));
  b.editor.double_click_at(b.grip("group:Group"));
  b.editor.settle();
  b.editor.right_click_at(b.grip("node:lfo"));
  REQUIRE(b.editor.click_on("menu:Remove from 'Group'"));
  CHECK(group_of(b.s, "Group")->members.size() == 2 && !b.drawn("node:lfo") && holds(b.s));
  b.editor.settle();
  REQUIRE(b.editor.click_on("crumb:0"));
  CHECK(b.drawn("node:lfo"));
  b.editor.right_click_at(b.grip("group:Group"));
  REQUIRE(b.editor.click_on("menu:Ungroup"));
  CHECK(b.s.open.patch.layout.groups.empty() && b.drawn("node:osc") && holds(b.s));
}

void run_rename_in_panel() {
  Bench b;
  group_osc_and_env(b);
  REQUIRE(b.editor.click_on("field:group name"));
  b.editor.press_key("A", false, true);
  b.editor.type_text("osc");
  b.editor.press_key("Enter");
  CHECK(group_of(b.s, "Group") && b.s.status == "name already in use by a node: osc");
  REQUIRE(b.editor.click_on("field:group name"));             // Enter let the field go
  b.editor.press_key("A", false, true);
  b.editor.type_text("Voice");
  b.editor.press_key("Enter");
  CHECK(group_of(b.s, "Voice") && b.s.selections == (Names{"Voice"}) && b.drawn("group:Voice") && holds(b.s));
}

void run_delete() {
  Bench b;
  const Patch before = b.s.open.patch;
  b.editor.click_at(b.grip("node:lfo"));
  b.editor.ctrl_click_at(b.grip("node:env"));
  b.editor.press_delete();                                    // the canvas has the keys since the click
  CHECK(!node_of(b.s, "lfo") && !node_of(b.s, "env") && b.s.selections.empty() && holds(b.s));
  b.editor.press_key("Z", false, true);
  CHECK(b.s.open.patch == before);
  b.editor.settle();
  group_osc_and_env(b);
  b.editor.click_at(b.grip("group:Group"));
  b.editor.press_delete();
  CHECK(b.s.open.patch.layout.groups.empty() && !node_of(b.s, "osc") && !node_of(b.s, "env") && holds(b.s));
  CHECK(b.s.status.rfind("Deleted 2 node(s)", 0) == 0 && !b.drawn("group:Group"));
  b.editor.press_key("Z", false, true);
  CHECK(group_of(b.s, "Group") && node_of(b.s, "osc"));
}

void run_listen() {
  Bench b;
  group_osc_and_env(b);
  b.editor.right_click_at(b.grip("group:Group"));
  REQUIRE(b.editor.click_on("menu:Listen here"));
  CHECK(b.s.listening == "osc" && b.drawn("mark:Group"));
  b.editor.double_click_at(b.grip("group:Group"));
  CHECK(b.s.groupPath == (Names{"Group"}) && b.s.listening == "osc");   // going in leaves it
  REQUIRE(b.editor.click_on("listen:Patch"));
  CHECK(b.s.listening.empty());
  REQUIRE(b.editor.click_on("listen:Group"));
  CHECK(b.s.listening == "osc");
  REQUIRE(b.editor.click_on("crumb:0"));                      // going up ends it
  CHECK(b.s.groupPath.empty() && b.s.listening.empty() && holds(b.s));
}

}  // namespace

void run_group_gesture_tests() {
  std::printf("groups by the mouse: make, go in and up, pins, add, remove, ungroup, rename, delete, listen\n");
  run_group_and_levels();
  run_group_pins();
  run_add_remove_ungroup();
  run_rename_in_panel();
  run_delete();
  run_listen();
}
