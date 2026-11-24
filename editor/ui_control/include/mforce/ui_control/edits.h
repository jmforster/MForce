#pragma once
#include <string>
#include <string_view>
#include "mforce/patch/layout.h"

namespace mforce::ui {

struct Session;

// What a view asks for. None throws: a refusal's sentence goes to
// session.status and nothing else changes. An edit that changes nothing
// (the same value again) leaves no undo step and does not touch the player.
// An edit that changes something is one undo step (a drag of a pin's
// constant, or of positions, is one step until end_gesture), and clears
// status first, so a refusal shows until the next change.

// A pin's constant: the sounding player follows it, or the sound follows
// the patch as sound_follows says (playing.h).
void edit_value   (Session& s, std::string_view node, std::string_view pin, float v);
// A scalar or a string setting: then the sound follows the patch.
void edit_setting (Session& s, std::string_view node, std::string_view name, float v);
void edit_string  (Session& s, std::string_view node, std::string_view name, std::string v);
// Where a node or a face (by its label), or the Output node, sits: no sound changes.
void edit_position(Session& s, std::string_view nodeOrFace, Vec2 pos);
void edit_output_position(Session& s, Vec2 pos);
void pan_canvas   (Session& s, Vec2 pos);            // kept in the patch; not a step and not an unsaved change
void select_node  (Session& s, std::string_view nodeOrFace);   // a node's id or a face's label; empty selects nothing
void arrange      (Session& s);                         // every node and face laid out afresh: one step; the sound is untouched

}  // namespace mforce::ui
