#include "mforce/ui_control/unsaved.h"
#include "mforce/ui_control/files.h"
#include "mforce/ui_control/session.h"

namespace mforce::ui {

namespace {

// What waited, done; the wait is over either way.
void do_what_waited(Session& s) {
  const Unsaved waited = s.unsaved;
  s.unsaved = Unsaved{};
  if (waited.waiting == Unsaved::Waiting::Open) open_file(s, waited.path);
  if (waited.waiting == Unsaved::Waiting::Close) s.unsaved.closing = true;
}

}  // namespace

void request_open(Session& s, const std::string& path) {
  if (!s.open.dirty) {
    open_file(s, path);
    return;
  }
  s.unsaved = Unsaved{Unsaved::Waiting::Open, path, false};
}

void request_close(Session& s) {
  if (!s.open.dirty) {
    s.unsaved.closing = true;
    return;
  }
  s.unsaved = Unsaved{Unsaved::Waiting::Close, {}, false};
}

void answer_unsaved(Session& s, Answer answer) {
  if (s.unsaved.waiting == Unsaved::Waiting::Nothing) return;
  switch (answer) {
    case Answer::Cancel:
      s.unsaved = Unsaved{};
      return;
    case Answer::Discard:
      do_what_waited(s);
      return;
    case Answer::Save:
      if (s.open.dirty) save_file(s);
      if (!s.open.dirty) do_what_waited(s);   // the patch has no file, or the save failed: the wait goes on
      return;
  }
}

}  // namespace mforce::ui
