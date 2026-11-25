#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace mforce::ui {

struct Session;

// What is selected: Session::selections, node ids, face labels and group
// names in the order chosen. Not saved, not an undo step. A rename renames
// an entry; a step that takes a thing away drops its entry; a change of
// level, New and Open select nothing.

void select_node (Session& s, std::string_view name);              // that one alone; empty: nothing
void select_nodes(Session& s, std::vector<std::string> names);    // these, each once, in this order
void toggle_selected(Session& s, std::string_view name);          // Ctrl and a click: out if in, else added
// The name when exactly one thing is selected; empty otherwise.
const std::string& selected_one(const Session& s);

}  // namespace mforce::ui
