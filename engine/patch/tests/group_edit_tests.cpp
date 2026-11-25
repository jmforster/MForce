// The group edits: what each changes and nothing else, each refusal, and a
// group that an edit leaves with no members going with its last member.
#include "edit_fixture.h"
#include <limits>

using namespace edit_fixture;

namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();

// The fixture's instrument with env and lfo in group G.
Patch grouped() {
  Patch p = instrument();
  p.layout.groups = {Group{"G", {"env", "lfo"}, Vec2{10, 20}}};
  return p;
}

// G holding lfo and Inner, Inner holding env.
Patch nested() {
  Patch p = instrument();
  p.layout.groups = {Group{"G", {"lfo", "Inner"}, Vec2{10, 20}}, Group{"Inner", {"env"}, Vec2{1, 2}}};
  return p;
}

void run_make_and_move() {
  Patch named = instrument();
  CHECK(fresh_group_name(named) == "Group");
  named.graph.nodes.push_back(node("Group", "SineSource"));
  CHECK(fresh_group_name(named) == "Group2");

  expect_applies("a group of two", instrument(),
                 [](Patch& p) { return add_group(p, "G", {"env", "lfo"}, Vec2{10, 20}, ""); }, grouped(),
                 Change::Layout);
  Patch want = grouped();
  want.layout.groups[0].members = {"lfo", "Inner"};
  want.layout.groups.push_back(Group{"Inner", {"env"}, Vec2{1, 2}});
  expect_applies("a group inside a group, its member leaving the outer", grouped(),
                 [](Patch& p) { return add_group(p, "Inner", {"env"}, Vec2{1, 2}, "G"); }, want, Change::Layout);
  want = instrument();
  want.layout.groups = {Group{"H", {"env", "lfo"}, Vec2{}}};
  expect_applies("a group that takes every member of another, which goes", grouped(),
                 [](Patch& p) { return add_group(p, "H", {"env", "lfo"}, Vec2{}, ""); }, want, Change::Layout);
  want = grouped();
  want.layout.groups[0].pos = Vec2{30, 40};
  expect_applies("a group moved", grouped(), [](Patch& p) { return move_group(p, "G", Vec2{30, 40}); }, want,
                 Change::Layout);

  expect_refused("no members", instrument(), [](Patch& p) { add_group(p, "G", {}, Vec2{}, ""); },
                 "group 'G': a group holds at least one member");
  expect_refused("a member that is not there", instrument(), [](Patch& p) { add_group(p, "G", {"nope"}, Vec2{}, ""); },
                 "'nope' names no node, group or face: it cannot join group 'G'");
  expect_refused("a parent that is not there", instrument(), [](Patch& p) { add_group(p, "G", {"env"}, Vec2{}, "P"); },
                 "'P' names no group: group 'G' cannot join it");
  expect_refused("a name taken by a node", instrument(), [](Patch& p) { add_group(p, "env", {"lfo"}, Vec2{}, ""); },
                 "'env' names two things: a node, a group");
  expect_refused("a group off the plane", instrument(),
                 [](Patch& p) { add_group(p, "G", {"env"}, Vec2{kInf, 0}, ""); },
                 "group 'G': its position (inf, 0) is not two finite numbers");
  expect_refused("a group that would hold its own parent", grouped(),
                 [](Patch& p) { add_group(p, "Outer", {"G"}, Vec2{}, "G"); }, "holds itself");
  expect_refused("no group to move", instrument(), [](Patch& p) { move_group(p, "G", Vec2{}); },
                 "'G' names no group: there is nothing to move");
}

void run_rename_and_remove() {
  Patch want = nested();
  want.layout.groups[1].name = "Core";
  want.layout.groups[0].members = {"lfo", "Core"};
  expect_applies("a group renamed, the group holding it following", nested(),
                 [](Patch& p) { return rename_group(p, "Inner", "Core"); }, want, Change::Layout);
  expect_applies("a group renamed to its own name", nested(), [](Patch& p) { return rename_group(p, "G", "G"); },
                 nested(), Change::Layout);
  expect_refused("a group renamed to a node's id", nested(), [](Patch& p) { rename_group(p, "Inner", "osc"); },
                 "'osc' names two things: a node, a group");
  expect_refused("no group to rename", nested(), [](Patch& p) { rename_group(p, "nope", "x"); },
                 "'nope' names no group: there is nothing to rename");

  want = instrument();
  want.layout.groups = {Group{"G", {"lfo", "env"}, Vec2{10, 20}}};
  expect_applies("a group taken apart, its members joining the group that held it, in its place", nested(),
                 [](Patch& p) { return remove_group(p, "Inner"); }, want, Change::Layout);
  want = instrument();
  want.layout.groups = {Group{"Inner", {"env"}, Vec2{1, 2}}};
  expect_applies("a group at the top taken apart, its members at the top", nested(),
                 [](Patch& p) { return remove_group(p, "G"); }, want, Change::Layout);
  Patch emptyInner = nested();
  emptyInner.layout.groups[0].members = {"Inner"};
  emptyInner.layout.groups[1].members.clear();
  expect_applies("the last member of a group taken apart: the holder goes too", emptyInner,
                 [](Patch& p) { return remove_group(p, "Inner"); }, instrument(), Change::Layout);
  expect_refused("no group to take apart", nested(), [](Patch& p) { remove_group(p, "nope"); },
                 "'nope' names no group: there is nothing to remove");
}

void run_regroup() {
  Patch want = grouped();
  want.layout.groups[0].members = {"lfo"};
  expect_applies("a member out of its group", grouped(), [](Patch& p) { return regroup(p, "env", ""); }, want,
                 Change::Layout);
  want = grouped();
  want.layout.groups[0].members = {"env", "lfo", "osc"};
  expect_applies("a node into a group", grouped(), [](Patch& p) { return regroup(p, "osc", "G"); }, want,
                 Change::Layout);
  expect_applies("a member into the group it is in", grouped(), [](Patch& p) { return regroup(p, "env", "G"); },
                 grouped(), Change::Layout);
  Patch chain = instrument();
  chain.layout.groups = {Group{"Outer", {"Inner"}, Vec2{}}, Group{"Inner", {"env"}, Vec2{}}};
  expect_applies("the last member out: its group goes, and the group that emptied in turn", chain,
                 [](Patch& p) { return regroup(p, "env", ""); }, instrument(), Change::Layout);
  expect_refused("a group into a group it holds", nested(), [](Patch& p) { regroup(p, "G", "Inner"); },
                 "holds itself");
  expect_refused("no member to regroup", grouped(), [](Patch& p) { regroup(p, "nope", "G"); },
                 "'nope' names no node, group or face: there is nothing to regroup");
  expect_refused("no group to join", grouped(), [](Patch& p) { regroup(p, "osc", "nope"); },
                 "'nope' names no group: 'osc' cannot join it");
}

// The record's other edits keep the rule: a remove that takes a group's last
// member takes the group, up through the groups that empty in turn; a group
// a file brought empty stays.
void run_emptied_by_removal() {
  Patch chain = instrument();
  chain.layout.groups = {Group{"Outer", {"Inner"}, Vec2{}}, Group{"Inner", {"lfo"}, Vec2{}}};
  Patch want = instrument();
  erase_node(want, "lfo");
  expect_applies("a node removed, the last member of nested groups", chain,
                 [](Patch& p) { return remove_node(p, "lfo"); }, want, Change::Structure);

  Patch faced = instrument();
  faced.layout.faces = {NoteFace{"Note1", {}, Vec2{}}};
  faced.layout.groups = {Group{"G", {"Note1"}, Vec2{}}};
  want = instrument();
  expect_applies("a face removed, the last member of its group", faced,
                 [](Patch& p) { return remove_face(p, "Note1"); }, want, Change::Structure);

  Patch brought = instrument();
  brought.layout.groups = {Group{"Empty", {}, Vec2{}}};
  want = brought;
  want.layout.positions["lfo"] = Vec2{5, 5};
  expect_applies("a group brought empty stays through another edit", brought,
                 [](Patch& p) { return move_node(p, "lfo", Vec2{5, 5}); }, want, Change::Layout);
}

}  // namespace

void run_group_edit_tests() {
  std::printf("group edits: make, move, rename, take apart, regroup; a group left empty goes\n");
  run_make_and_move();
  run_rename_and_remove();
  run_regroup();
  run_emptied_by_removal();
}
