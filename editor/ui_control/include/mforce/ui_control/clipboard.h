#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include "mforce/patch/layout.h"
#include "mforce/ui_control/wiring.h"

namespace mforce::ui {

struct Session;

// Copy, cut and paste as a fragment's text (patch/fragment_text.h): the
// text is the view's to put on the system's clipboard and to read from it,
// so nothing here touches a clipboard. Each change is one undo step; a
// refusal goes to status and nothing changes.

// The selection as a fragment's text. A Note, a group, a Channel, a Mixer
// and a name that is gone are skipped and counted: "Copied N node(s)", with
// " (uncopyable selection skipped)" when any was. Empty, with "Copy: nothing
// selected" or "Copy: nothing copyable selected (Note/Output/Mixer/groups
// are v2)" in status, when nothing can be copied.
std::string copy_text(Session& s);
// The fragment `text` holds joins the patch: with `at` (where the create
// menu was opened) its first node lands there; without, at the copied
// places plus (60, 60) times the least k from 1 at which no node of the
// patch sits where its first node would land. The pasted nodes join the
// group that is open and become the selection; listening is left alone.
// "Pasted N node(s) (first, ...)", or with " — K unresolvable ref(s)
// dropped"; "Paste: clipboard empty", "Paste: clipboard is not an MForce
// fragment", "Paste: fragment had no nodes", "Paste failed (graph
// restored): <why>".
void paste_text(Session& s, std::string_view text, std::optional<Vec2> at);
// copy_text, then the things copied deleted (none that was skipped): one
// step. The copy's words stay in status.
std::string cut_text(Session& s);
// A copy and a paste of the one node, at (30, 30) from it, into the group
// that is open, without the clipboard: its constants, settings, strings,
// arrays, dynamic pins, seed and the wires into it. It becomes the
// selection. Refused on a Note, a Channel and a Mixer.
void duplicate(Session& s, std::string_view node);
// The node made a node of `type` (replace_node); the selection and
// listening follow to the new node. "Replaced A with B (N wire(s) kept)",
// or with "; dropped: x, y" naming what had no place. Refused on a Note,
// a Channel and a Mixer.
void replace_with(Session& s, std::string_view node, std::string_view type);
// The wire into `reader` (an Input's feed `feed`, or the patch's output) runs
// through a new pair of Wormholes: the in-half beside the source, fed as the
// wire was (a tap stays a tap at the source), and the out-half beside the
// reader, each in its end's group. One step; the status says the pair's
// words. Refused, in words, on a setting's drive and on a wire that touches
// a Wormhole already.
void route_wire(Session& s, const PinEnd& reader, std::size_t feed);

}  // namespace mforce::ui
