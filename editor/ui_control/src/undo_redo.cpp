#include <optional>
#include "mforce/ui_control/building.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_model/history.h"
#include "stepping.h"

namespace mforce::ui {

namespace {

// Whether the build reads anything that differs between `a` and `b`: the
// graph, the instrument block, the sample rate.
bool sound_differs(const Patch& a, const Patch& b) {
  return !(a.graph == b.graph) || !(a.instrument == b.instrument) || a.sampleRate != b.sampleRate;
}

// One move through the history by `move` (ui_model's undo or redo): the
// panning stays, what went is forgotten. The sound follows only when the
// move changed what the build reads, so undoing a move of a node does not
// cut the notes that sound; with no player (a patch that does not sound),
// sound_follows says why again.
void travel(Session& s, bool (*move)(History&, Patch&), const char* nothing) {
  s.gesture.clear();
  const std::optional<Vec2> panning = s.open.patch.layout.panning;
  const Patch before = s.open.patch;
  if (!move(s.open.history, s.open.patch)) {
    s.status = nothing;
    return;
  }
  s.open.patch.layout.panning = panning;
  s.status.clear();
  const bool listeningEnded = stepping::forget_what_went(s);
  if (sound_differs(before, s.open.patch) || !s.live.player() || listeningEnded) sound_follows(s);
}

}  // namespace

void undo(Session& s) { travel(s, &mforce::ui::undo, "nothing to undo"); }

void redo(Session& s) { travel(s, &mforce::ui::redo, "nothing to redo"); }

}  // namespace mforce::ui
