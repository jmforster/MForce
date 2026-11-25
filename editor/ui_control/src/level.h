#pragma once
#include <optional>
#include <string>
#include <vector>
#include "mforce/ui_control/session.h"

// Where the canvas is (Session::groupPath) and the panning of each level,
// shared by the control's files.
namespace mforce::ui::level {

// The path becomes `to`. A change of level selects nothing. Going deeper
// remembers the panning left and frames the new level; going up restores
// the panning left at that depth; anything else frames the level, so a
// level never opens on a blank pane. Any change but going deeper ends
// listening: backing out returns the ears to the patch at any level. True
// when it ended listening: the caller has the sound follow.
bool set_path(Session& s, std::vector<std::string> to);

// The path cut where it stops naming a level (valid_depth), as set_path
// does it; true when that ended listening.
bool follow_path(Session& s);

// Where the record places a thing a level shows: a node, a face or a
// group; none for a thing with no position.
std::optional<Vec2> place_of(const Patch& p, const std::string& name);

// The panning that puts the least corner of what the level `path` shows
// 80 points in from the canvas's corner.
Vec2 framed(const Patch& p, const std::vector<std::string>& path);

}  // namespace mforce::ui::level
