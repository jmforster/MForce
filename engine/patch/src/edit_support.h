#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "mforce/patch/edit.h"
#include "mforce/patch/type_declarations.h"

// What the edit files (edit.cpp, edit_nodes.cpp, edit_wires.cpp,
// edit_faces.cpp) share: finding what an edit names, the one way a
// structural edit is accepted, and taking a node out of a patch.
namespace mforce::edit_detail {

// The first node with id `id`, as a feed of that id reads it. `after` ends
// the refusal: what the edit would have changed.
Node& node_named(Patch& p, std::string_view id, const std::string& after);

// A node, its declarations, and the words a sentence about one of its names
// opens with: "node 'env' (Envelope): 'timeScale'".
struct NamedOnNode {
  Node& node;
  const TypeDeclarations& d;
  std::string head;
};
// Refused when there is no such node or its type is not registered.
NamedOnNode named_on_node(Patch& p, std::string_view id, std::string_view name);

// Refused when a coordinate of `pos` is not finite; `what` names the point.
void require_finite(Vec2 pos, const std::string& what);

// A line of check() without its record path: the text after the first ": ".
std::string check_sentence(const std::string& line);

// The edit made on `copy` is accepted when check() finds nothing in it:
// `copy` becomes the patch and `kind` is returned. Otherwise the first line's
// sentence is thrown and `p` is left as it was.
Change adopt(Patch& p, Patch& copy, Change kind);

// The index of the first node with id `id`; none when no node has it.
std::optional<std::size_t> index_of(const Patch& p, std::string_view id);

// True when a node id, a group name or a face label is `name`: they share
// one namespace.
bool name_taken(const Patch& p, std::string_view name);
// `base` when it is free and `bareFirst`; else `base` followed by the first
// number that makes it free, from 2 when `bareFirst` and from 1 when not.
std::string free_name(const Patch& p, const std::string& base, bool bareFirst);

// A pin of `n` whose last feed went: its entry in `wires` goes, and a Param
// takes its declared default.
void pin_lost_its_feeds(Node& n, const std::string& pin);

// Node `index` goes, and everything that named it: each feed that read it,
// each dynamic pin it fed, the output when it was, its position, its place
// in groups and its field of a face. Returns the ids the node itself read,
// for drop_unread_face_nodes.
std::vector<std::string> detach_node(Patch& p, std::size_t index);

// Each id of `wereRead` (nodes an edit stopped reading) that a face names
// for a field, and that no feed, dynamic pin or the output reads any more,
// goes as detach_node takes it, with its field; what it read is looked at
// the same way.
void drop_unread_face_nodes(Patch& p, std::vector<std::string> wereRead);

}  // namespace mforce::edit_detail
