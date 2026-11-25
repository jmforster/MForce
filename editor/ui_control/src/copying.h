#pragma once
#include <string>
#include <string_view>
#include "mforce/ui_control/session.h"

// What the clipboard, Duplicate, Replace with and a new node share.
namespace mforce::ui::copying {

// What `name` is when it is not a node a copy takes, in a word: "a Note"
// (a face, or a face's node), "a group", "a Channel", "a Mixer", or
// "nothing" for a name that is gone; empty for a node that is copied.
std::string uncopyable(const Patch& p, std::string_view name);

// `name`, just made, joins the group the canvas shows (none at the top).
void join_open(Patch& p, const Session& s, const std::string& name);

}  // namespace mforce::ui::copying
