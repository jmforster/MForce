#pragma once
#include <cstddef>
#include <string>

namespace mforce::ui {

struct Session;

// One end of a wire as the canvas names it.
struct PinEnd {
  enum class Kind { Output, Tap, NoteField, Input, SettingPin, PatchOutput };
  Kind        kind;
  std::string node;     // a node id, or a face label for a NoteField; empty for the PatchOutput
  std::string name;     // the pin, setting or field; empty for an Output, a Tap or the PatchOutput
};

// The two ends of a drag, in either order: one gives a signal (an Output, a
// Tap, a NoteField) and the other takes it (an Input, a SettingPin, the
// PatchOutput). The source is the node, through a tap for a Tap, or for a
// NoteField the node the face names for that field, made now when it has
// none. Then the pin is fed (a pin that takes one feed loses the one it had),
// the setting driven once per note, or the patch's output set. One undo step;
// the sound follows. Anything else, a refusal of the engine's among them,
// goes to status and changes nothing.
void wire  (Session& s, const PinEnd& a, const PinEnd& b);
// The reader's feed number `feed` goes (an Input); the setting's drive goes,
// its pin staying on offer (a SettingPin; `feed` unused); the patch has no
// output (the PatchOutput). One undo step; the sound follows.
void unwire(Session& s, const PinEnd& reader, std::size_t feed);

}  // namespace mforce::ui
