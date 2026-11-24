// History: undo and redo by copies of the patch, the cap of 200, and the
// saved state through saves, undos past it and new edits.
#include "mforce/testing/check.h"
#include "mforce/ui_model/history.h"
#include "mforce/ui_model/open_patch.h"

using namespace mforce;
using namespace mforce::ui;

namespace {

// A patch told apart from others by its sample rate.
Patch numbered(int k) {
  Patch p;
  p.sampleRate = k;
  return p;
}

// `patch` replaced by numbered(k), as an edit does.
void edit_to(History& h, Patch& patch, int k) {
  remember(h, patch);
  patch = numbered(k);
}

}  // namespace

void run_history_tests() {
  History h;
  Patch p = numbered(0);
  CHECK(!unsaved(h));
  CHECK(!undo(h, p) && !redo(h, p) && p == numbered(0));
  edit_to(h, p, 1);
  edit_to(h, p, 2);
  CHECK(unsaved(h));
  CHECK(undo(h, p) && p == numbered(1));
  CHECK(undo(h, p) && p == numbered(0));
  CHECK(!unsaved(h));                                  // back at the state the file holds
  CHECK(redo(h, p) && p == numbered(1) && unsaved(h));
  CHECK(redo(h, p) && p == numbered(2));
  CHECK(!redo(h, p));

  mark_saved(h);                                       // saved at 2
  CHECK(!unsaved(h));
  CHECK(undo(h, p) && p == numbered(1) && unsaved(h));
  edit_to(h, p, 3);                                    // the saved state was in the future that goes
  CHECK(h.future.empty() && h.savedAt == -1 && unsaved(h));
  CHECK(undo(h, p) && unsaved(h));
  CHECK(undo(h, p) && unsaved(h));                     // unsaved for good, until saved
  mark_saved(h);
  CHECK(!unsaved(h));
}

void run_history_cap_tests() {
  History h;
  Patch p = numbered(0);
  for (int k = 1; k <= 199; ++k) edit_to(h, p, k);
  mark_saved(h);                                       // saved with 199 kept
  edit_to(h, p, 200);
  CHECK(h.past.size() == 200 && h.savedAt == 199);
  edit_to(h, p, 201);                                  // the oldest goes; the saved state is still kept
  CHECK(h.past.size() == 200 && h.savedAt == 198 && h.past.front() == numbered(1));
  CHECK(undo(h, p) && undo(h, p) && !unsaved(h) && p == numbered(199));
  CHECK(redo(h, p) && redo(h, p) && p == numbered(201));

  History gone;
  Patch q = numbered(0);
  for (int k = 1; k <= 201; ++k) edit_to(gone, q, k);  // the saved state (0) dropped off the end
  CHECK(gone.past.size() == 200 && gone.savedAt == -1);
  int undone = 0;
  while (undo(gone, q)) ++undone;
  CHECK(undone == 200 && q == numbered(1) && unsaved(gone));

  OpenPatch open;
  CHECK(!unsaved(open));
  remember(open.history, open.patch);
  CHECK(unsaved(open));
}
