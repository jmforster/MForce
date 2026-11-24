#pragma once
#include <map>
#include <optional>
#include <string>
#include "mforce/patch/layout.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_views/file_dialogs.h"

namespace mforce::ui {

// The transport, a strip across the top of the window: four tabs (Note,
// Passage, Chords, Drums), the shown one the transport's mode, each with its
// fields; then Generate (reading "Generating..." on the frame before the
// render runs), Play (reading Stop while anything plays) and Save WAV; and
// at the right, the last note played.
//   Note: the pitch (C to B) and octave, which compose the note's text and
//     move the keyboard's octave; velocity; duration (the keyboard's note
//     length); Live.
//   Passage: the text; octave; BPM; velocity; Onsets; Save and Load Passage.
//   Chords: the text; Group; Figure; Save and Load Chords; octave; BPM; Delay.
//   Drums: the pattern; the drum map; BPM.
// A field applies as it is typed: the transport is not the patch, so no
// undo step and no unsaved change.
class TransportView {
public:
  explicit TransportView(FileDialogs& dialogs);
  void draw(Session& s);
  // Where the tab or button with this label was drawn last frame, its
  // centre; none when it was not drawn. For a test to click it.
  std::optional<Vec2> centre_of(const std::string& label) const;

private:
  void tabs(Session& s);
  void note_fields(Session& s);
  void passage_fields(Session& s);
  void chords_fields(Session& s);
  void drums_fields(Session& s);
  void buttons(Session& s);
  void last_note_text(const Session& s);
  void text_buttons(Session& s, const char* save, const char* load, FileKind kind);
  void remember(const std::string& label);         // the last item drawn, under `label`

  FileDialogs& dialogs_;
  std::map<std::string, Vec2> centres_;            // drawing state: where each named item was last drawn
};

}  // namespace mforce::ui
