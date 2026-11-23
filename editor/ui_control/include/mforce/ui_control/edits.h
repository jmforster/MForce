#pragma once
#include <string>
#include <string_view>
#include "mforce/patch/layout.h"

namespace mforce::ui {

struct Session;

// What a view asks for. None throws: a refusal's sentence goes to
// session.status and nothing else changes. An edit that changes nothing
// (the same value again) does not mark the patch unsaved and does not
// rebuild the player. A rebuild that fails puts its sentence in status; the
// patch keeps the edit.

// A pin's constant: the sounding player follows it, or is rebuilt.
void edit_value   (Session& s, std::string_view node, std::string_view pin, float v);
// A scalar or a string setting: the player is rebuilt.
void edit_setting (Session& s, std::string_view node, std::string_view name, float v);
void edit_string  (Session& s, std::string_view node, std::string_view name, std::string v);
// Where a node, or the Output node, sits: no sound changes.
void edit_position(Session& s, std::string_view node, Vec2 pos);
void edit_output_position(Session& s, Vec2 pos);
void pan_canvas   (Session& s, Vec2 pos);            // kept in the patch, and not an unsaved change
void select_node  (Session& s, std::string_view node);   // empty selects nothing

}  // namespace mforce::ui
