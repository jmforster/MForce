// The edits of groups: a group made, moved, renamed, taken apart, and a
// member moved from one group to another. A group is the editor's, so each
// is Change::Layout.
#include "mforce/patch/edit.h"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>
#include "edit_support.h"
#include "sentence.h"

namespace mforce {

using namespace edit_detail;   // the shared helpers by their short names (C#: a `using` directive)

namespace {

std::string group_opening(std::string_view name) { return "group " + in_quotes(name) + ": "; }

// The group named `name` in `p`; refused, ending with `after`, when none is.
Group& existing_group(Patch& p, std::string_view name, const std::string& after) {
  if (Group* g = group_named(p, name)) return *g;
  throw std::runtime_error(in_quotes(name) + " names no group: " + after);
}

// Refused when `member` is no node, face or group of `p`.
void require_member(const Patch& p, std::string_view member, const std::string& after) {
  if (!name_taken(p, member)) throw std::runtime_error(in_quotes(member) + " names no node, group or face: " + after);
}

}  // namespace

std::string fresh_group_name(const Patch& p) { return free_name(p, "Group", true); }

Change add_group(Patch& p, std::string_view name, const std::vector<std::string>& members, Vec2 pos,
                 std::string_view parent) {
  if (members.empty()) throw std::runtime_error(group_opening(name) + "a group holds at least one member");
  for (const auto& m : members) require_member(p, m, "it cannot join group " + in_quotes(name));
  require_finite(pos, group_opening(name) + "its position");
  Patch copy = p;
  if (!parent.empty()) (void)existing_group(copy, parent, "group " + in_quotes(name) + " cannot join it");
  for (const auto& m : members)
    if (Group* holder = group_holding(copy, m)) std::erase(holder->members, m);
  copy.layout.groups.push_back(Group{std::string(name), members, pos});
  if (!parent.empty()) group_named(copy, parent)->members.push_back(std::string(name));
  return adopt(p, copy, Change::Layout);
}

Change move_group(Patch& p, std::string_view name, Vec2 pos) {
  Patch copy = p;
  Group& g = existing_group(copy, name, "there is nothing to move");
  require_finite(pos, group_opening(name) + "its position");
  g.pos = pos;
  return adopt(p, copy, Change::Layout);
}

Change rename_group(Patch& p, std::string_view name, std::string_view to) {
  Patch copy = p;
  Group& g = existing_group(copy, name, "there is nothing to rename");
  if (g.name == to) return Change::Layout;
  g.name = std::string(to);
  if (Group* holder = group_holding(copy, name)) std::replace(holder->members.begin(), holder->members.end(),
                                                              std::string(name), std::string(to));
  return adopt(p, copy, Change::Layout);
}

Change remove_group(Patch& p, std::string_view name) {
  Patch copy = p;
  const Group gone = existing_group(copy, name, "there is nothing to remove");
  std::erase_if(copy.layout.groups, [&](const Group& g) { return g.name == name; });
  if (Group* holder = group_holding(copy, name)) {
    auto& list = holder->members;
    const auto at = list.erase(std::find(list.begin(), list.end(), name));
    list.insert(at, gone.members.begin(), gone.members.end());
  }
  return adopt(p, copy, Change::Layout);
}

Change regroup(Patch& p, std::string_view member, std::string_view group) {
  require_member(p, member, "there is nothing to regroup");
  Patch copy = p;
  if (!group.empty()) (void)existing_group(copy, group, in_quotes(member) + " cannot join it");
  Group* holder = group_holding(copy, member);
  if (holder ? holder->name == group : group.empty()) return Change::Layout;
  if (holder) std::erase(holder->members, member);
  if (!group.empty()) group_named(copy, group)->members.push_back(std::string(member));
  return adopt(p, copy, Change::Layout);
}

} // namespace mforce
