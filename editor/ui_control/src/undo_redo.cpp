#include <optional>
#include "mforce/ui_control/building.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_model/history.h"
#include "stepping.h"

namespace mforce::ui {

namespace {

// One move through the history by `move` (ui_model's undo or redo): the
// panning stays, what went is forgotten, the sound follows.
void travel(Session& s, bool (*move)(History&, Patch&), const char* nothing) {
  s.gesture.clear();
  const std::optional<Vec2> panning = s.open.patch.layout.panning;
  if (!move(s.open.history, s.open.patch)) {
    s.status = nothing;
    return;
  }
  s.open.patch.layout.panning = panning;
  s.status.clear();
  stepping::forget_what_went(s);
  sound_follows(s);
}

}  // namespace

void undo(Session& s) { travel(s, &mforce::ui::undo, "nothing to undo"); }

void redo(Session& s) { travel(s, &mforce::ui::redo, "nothing to redo"); }

}  // namespace mforce::ui
