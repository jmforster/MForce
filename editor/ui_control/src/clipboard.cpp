// Copy, cut and paste through a fragment's text.
#include "mforce/ui_control/clipboard.h"
#include <exception>
#include <string>
#include <vector>
#include "mforce/patch/edit.h"
#include "mforce/patch/fragment_text.h"
#include "mforce/ui_control/building.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/selecting.h"
#include "mforce/ui_control/session.h"
#include "copying.h"
#include "stepping.h"

namespace mforce::ui {

using namespace stepping;   // the shared helpers by their short names (C#: a `using` directive)

namespace {

constexpr float kPasteStep = 60.0f;   // so a paste into the same patch does not land on the originals

// The selection's nodes a copy takes, in the order chosen; `skipped` counts
// the rest.
std::vector<std::string> copyable(const Session& s, int& skipped) {
  std::vector<std::string> ids;
  skipped = 0;
  for (const auto& name : s.selections) {
    if (copying::uncopyable(s.open.patch, name).empty()) ids.push_back(name);
    else ++skipped;
  }
  return ids;
}

// Where the fragment's first node would land: at `at`, or the first step of
// (60, 60) from its own place at which no node of the patch sits.
Vec2 paste_offset(const Patch& p, const Fragment& f, std::optional<Vec2> at) {
  const auto own = f.positions.find(f.nodes.front().id);
  const Vec2 from = own == f.positions.end() ? Vec2{} : own->second;
  if (at) return Vec2{at->x - from.x, at->y - from.y};
  for (int k = 1;; ++k) {
    const Vec2 by{kPasteStep * float(k), kPasteStep * float(k)};
    bool taken = false;
    for (const auto& [id, pos] : p.layout.positions) taken = taken || pos == Vec2{from.x + by.x, from.y + by.y};
    if (!taken) return by;
  }
}

std::string plural(int n, const char* word) { return std::to_string(n) + " " + word + (n == 1 ? "" : "s"); }

// The words for a paste done.
std::string pasted_words(const Pasted& done) {
  const int n = int(done.ids.size());
  if (done.dropped)
    return "Pasted " + plural(n, "node") + " (" + done.ids.front() + "...) — " + plural(done.dropped, "unresolvable ref") +
           " dropped";
  return "Pasted " + plural(n, "node") + " (" + done.ids.front() + (n > 1 ? ", ..." : "") + ")";
}

}  // namespace

std::string copy_text(Session& s) {
  if (s.selections.empty()) {
    s.status = "Copy: nothing selected";
    return {};
  }
  int skipped = 0;
  const std::vector<std::string> ids = copyable(s, skipped);
  if (ids.empty()) {
    s.status = "Copy: nothing copyable selected (Note/Output/Mixer/groups are v2)";
    return {};
  }
  const std::string text = fragment_text(fragment_of(s.open.patch, ids));
  s.status = "Copied " + plural(int(ids.size()), "node") + (skipped ? " (uncopyable selection skipped)" : "");
  return text;
}

void paste_text(Session& s, std::string_view text, std::optional<Vec2> at) {
  if (text.empty()) {
    s.status = "Paste: clipboard empty";
    return;
  }
  Fragment f;
  try {
    f = fragment_from_text(text);
  } catch (const std::exception& e) {
    const std::string why = e.what();
    const bool plain = why == "clipboard is not an MForce fragment" || why == "fragment had no nodes";
    s.status = (plain ? "Paste: " : "Paste failed: ") + why;
    return;
  }
  const Vec2 offset = paste_offset(s.open.patch, f, at);
  Pasted done;
  if (!step(s, "", [&](Patch& p) {
        done = paste(p, f, offset);
        for (const auto& id : done.ids) (void)copying::join_open(p, s, id);
        return Change::Structure;
      })) {
    // The engine's paste refuses before it changes anything; only a refusal
    // after it applied (joining the open group) had a patch to put back.
    s.status = (done.ids.empty() ? "Paste failed: " : "Paste failed (graph restored): ") + s.status;
    return;
  }
  select_nodes(s, done.ids);
  sound_follows(s);
  s.status = pasted_words(done);
}

// A cut never deletes what it did not copy: a selected group stays, and
// Delete is the way to remove one.
std::string cut_text(Session& s) {
  int skipped = 0;
  const std::vector<std::string> ids = copyable(s, skipped);
  const std::string text = copy_text(s);
  if (text.empty()) {
    s.status = s.selections.empty() ? "Cut: nothing selected"
                                     : "Cut: nothing copyable selected (Note/Output/Mixer/groups are v2) — Delete removes a group";
    return {};
  }
  const std::string words = s.status;
  delete_these(s, {}, ids);
  for (const auto& id : ids)
    if (has_node(s.open.patch, id)) return {};    // the delete was refused: its words are in status
  s.status = words;
  return text;
}

}  // namespace mforce::ui
