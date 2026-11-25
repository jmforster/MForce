#pragma once
#include <cstddef>
#include <optional>
#include <string_view>
#include "mforce/patch/layout.h"

namespace mforce::ui {

struct Session;

// Groups and the level the canvas shows (Session::groupPath, not saved).
// Each edit is one undo step; a refusal goes to status and nothing changes.
// The path follows every step: a rename of a group in it renames the entry,
// and a step (an ungroup, a delete, a regroup, an undo or a redo) that
// leaves an entry that is no group, or not held by the entry before it,
// cuts the path there. A change of level selects nothing, and any change
// but going deeper ends listening.

// The selection made one group at the level shown: its members leave the
// group that is open and the new group joins it, at the centroid of their
// positions ((200, 200) when none has one). "Group refused: <why>" (the
// two-output rule's words) or "Grouped as '<name>' — double-click to open,
// rename in Properties".
void group_selection  (Session& s);
// The selected nodes and faces join `group`, after the two-output rule on a
// trial membership: "Add refused: '<group>' would have N outputs: a b", or
// "N node(s) -> '<group>'".
void add_to_group     (Session& s, std::string_view group);
// Each selected node and face leaves its group for the group that held it,
// or the top: "removed from group".
void remove_from_group(Session& s);
// The group goes; its members join the group that held it, or the top.
void ungroup          (Session& s, std::string_view group);
// Into the group, which must be shown at this level; listening stays.
void open_group       (Session& s, std::string_view group);
// The breadcrumb: up to `depth` (0 is the patch). Ends listening.
void go_up            (Session& s, std::size_t depth);
// To the level `thing` (a node, a face or a group) is shown at, and it
// alone selected; with `canvasSize`, the canvas's panning centres it.
void go_to            (Session& s, std::string_view thing, std::optional<Vec2> canvasSize);
// Listening at the group's output_node; refused for a group that has none.
void listen_at_group  (Session& s, std::string_view group);
// The panning the canvas shows: the patch's at the top, the level's inside
// a group.
Vec2 shown_panning    (const Session& s);

}  // namespace mforce::ui
