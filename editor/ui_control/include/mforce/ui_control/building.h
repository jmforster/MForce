#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "mforce/patch/instrument_block.h"
#include "mforce/patch/layout.h"
#include "mforce/ui_control/wiring.h"

namespace mforce::ui {

struct Session;

// What a view asks for to build a patch. None throws: a refusal goes to
// status and nothing changes. Each change is one undo step; after a change
// the build reads, the sound follows the patch (sound_follows).

// File > New: a new patch (new_file) at once, or, with unsaved changes,
// waiting for the answer as request_open does.
void request_new (Session& s);
// A node of `type` at `pos`, with a fresh id and a seed from session.seeds;
// it is selected.
void create_node (Session& s, std::string_view type, Vec2 pos);
// A note face at `pos`, with a fresh label; it is selected. Refused on a
// plain sound, which has no note to read.
void create_note (Session& s, Vec2 pos);
// The wires first (each a reader and its feed's number; several feeds of one
// pin from the highest number down), then each name: a node's id or a face's
// label, one already gone by then skipped. One step. The selection goes when
// it named something that went, and so do the offers of a node that went.
void delete_these(Session& s, const std::vector<std::pair<PinEnd, std::size_t>>& wires,
                  const std::vector<std::string>& nodes);
// A new name for a node or a face, refused first by the editor's rules for a
// name a person types: not empty, no ".", no leading "__" (it marks a face's
// nodes), not in use by a node, a face or a group. The selection and the
// offers follow.
void rename      (Session& s, std::string_view nodeOrFace, std::string to);
// The instrument's own settings. Refused on a plain sound.
void edit_instrument(Session& s, const InstrumentBlock& block);
// Shows the pin of `setting` on `node`, to be wired. Refused, in check()'s
// words, on a structural setting and on a plain sound: a pin that could
// never be fed is never shown. Withdrawing drops the offer, and the
// setting's drive when it has one (a step).
void offer_setting_pin   (Session& s, std::string_view node, std::string_view setting);
void withdraw_setting_pin(Session& s, std::string_view node, std::string_view setting);
// Back or forward one step. The drag ends; the canvas's panning stays where it
// is; the selection stays when what it names is still there; the offers of
// nodes that are not go; the sound follows. Nothing to undo or redo says so
// in status.
void undo(Session& s);
void redo(Session& s);
// The drag under way is over: the next edit is a step of its own.
void end_gesture(Session& s);
// The panning that brings the whole patch into view (fit_panning). Not a step.
void fit_canvas(Session& s);
// The recent files emptied; the list written at exit is then empty.
void clear_recent(Session& s);

}  // namespace mforce::ui
