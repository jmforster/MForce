#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include "mforce/patch/patch.h"

namespace mforce::ui {

// What a group shows, worked out from the wiring each time it is asked:
// nothing here is stored. A group's pins are never authored; its boundary
// edges are its interface. A span between two Wormholes crosses no edge.

// A wire or a drive that reads, inside the group, something outside it.
struct Crossing {
  std::string node;             // the member that reads
  std::string pin;              // its pin, or for a drive its setting
  std::size_t feed{0};          // the feed's number on the pin; 0 for a drive
  std::string source;           // the node outside that it reads
  bool tap{false};
  bool drive{false};            // a setting driven once a note, not a pin's feed
};

// A member that something outside the group reads (a feed, a drive, the
// patch's output), through its output or its tap: one each, however many
// wires leave from it.
struct Leaving {
  std::string node;
  bool tap{false};
  // A group's output for the two-output rule: false when only drives read
  // it, or it is a note face's field: a face broadcasts the note's fields
  // and is never a group's signal output.
  bool counted{true};
};

struct Boundary {
  std::vector<Crossing> in;     // one a wire or drive, in the patch's order of nodes
  std::vector<Leaving> out;     // one a source and kind (plain or tap)
};

Boundary boundary(const Patch& patch, std::string_view group);
// The graph nodes under a group: its member nodes, its member faces' nodes,
// and those of the groups it holds, in the patch's order of nodes.
std::vector<std::string> nodes_under(const Patch& patch, std::string_view group);
// The group that lists `member`; empty when none does.
std::string group_holding(const Patch& patch, std::string_view member);
// The groups that hold `thing`, outermost first: the path to its level.
std::vector<std::string> path_to(const Patch& patch, std::string_view thing);
// The one member whose signal leaves the group (a counted output); with
// none leaving, the last member in signal order that is a signal and no
// face's node. Empty when there is none, when more than one leaves, or when
// the one that leaves is a part: such a group has no "Listen here".
std::string output_node(const Patch& patch, std::string_view group);
// Why `members` (node ids, face labels, group names) cannot be one group,
// without the "Group refused: " the caller puts first: "nothing selected",
// "nothing groupable selected", "selection has 2 outputs: a b"; empty when
// they can. A name that is no node, face or group is not groupable, nor is
// a face's own node.
std::string group_refusal(const Patch& patch, const std::vector<std::string>& members);
// The outputs a set of members would have as one group: the counted
// leaving nodes, each once, in the patch's order.
std::vector<std::string> group_outputs(const Patch& patch, const std::vector<std::string>& members);
// What the canvas draws at one level: the nodes (never a face's own node),
// then the faces, then the groups, whose holder is the last name of `path`
// (none: the top).
std::vector<std::string> shown_at(const Patch& patch, const std::vector<std::string>& path);
// How long a prefix of `path` still names a level: each entry a group, the
// first held by none, each later one held by the one before it.
std::size_t valid_depth(const Patch& patch, const std::vector<std::string>& path);

}  // namespace mforce::ui
