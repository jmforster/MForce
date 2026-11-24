#pragma once
#include <vector>
#include "mforce/patch/patch.h"

namespace mforce::ui {

// The patches an edit replaced, so each can be taken back. A patch is a
// plain value, so "the patch before" is a copy of it (the memento pattern).
struct History {
  std::vector<Patch> past;      // oldest first, at most 200
  std::vector<Patch> future;    // what redo returns to, nearest last
  int savedAt{0};               // how many of `past` there were when the file was read or saved; -1 once that state is gone
};

// An edit is about to replace `before`: keep it, drop the future. The oldest
// goes beyond 200.
void remember(History& history, const Patch& before);
// `patch` goes onto the future and the newest of the past comes back into
// it; false, and nothing changes, when there is nothing to undo.
bool undo(History& history, Patch& patch);
// The reverse of undo; false when there is nothing to redo.
bool redo(History& history, Patch& patch);
// The patch as it is now is the one in its file.
void mark_saved(History& history);
// The patch is not the one in its file.
bool unsaved(const History& history);

}  // namespace mforce::ui
