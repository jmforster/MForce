#include "mforce/ui_control/files.h"
#include <exception>
#include <filesystem>
#include <string>
#include <utility>
#include "mforce/perform/embedded_score.h"
#include "mforce/ui_control/generating.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_model/new_patch.h"
#include "mforce/ui_model/note_faces.h"
#include "mforce/ui_model/placement.h"

namespace mforce::ui {

namespace {

// The note that the file's rate is not the output's; empty when they agree
// or nothing plays.
std::string rate_note(const Session& s) {
  const Patch& p = s.open.patch;
  if (!s.live.playing() || p.sampleRate == s.audio.sample_rate()) return {};
  return "the file is at " + std::to_string(p.sampleRate) + " Hz and plays here at " +
         std::to_string(s.audio.sample_rate()) + " Hz, the output's rate; a render uses the file's " +
         std::to_string(p.sampleRate) + " Hz";
}

// `next` becomes the open patch, as one that was never edited: nothing
// selected, offered, waiting, listened to or in a drag; no strip popped out;
// the take dropped and its playback stopped; the views told; the sound follows.
void take(Session& s, OpenPatch next) {
  (void)gather_note_faces(next.patch);
  (void)drop_unread_face_nodes(next.patch);
  (void)place_unplaced_nodes(next.patch);
  s.open = std::move(next);
  s.selections.clear();
  s.groupPath.clear();
  s.pans.clear();
  s.levelPanning = Vec2{};
  s.offered.clear();
  s.gesture.clear();
  s.unsaved = Unsaved{};
  s.listening.clear();
  s.popOuts.clear();
  replace_take(s, Take{});
  s.runningPlain = nullptr;
  s.audio.play(AudioAdapter::Line::Playback, nullptr);
  ++s.opened;
  s.status.clear();
  sound_follows(s);
  if (s.status.empty()) s.status = rate_note(s);
}

}  // namespace

void open_file(Session& s, const std::string& path) {
  OpenPatch next;
  try {
    next = open_patch(path);
  } catch (const std::exception& e) {
    s.status = e.what();
    if (!std::filesystem::exists(path)) forget(s.recent, path);
    return;
  }
  take(s, std::move(next));
  seed_from_score(s);
  note_opened(s.recent, path);
}

void new_file(Session& s) {
  const std::uint32_t sineSeed = s.seeds(), envelopeSeed = s.seeds();
  Patch fresh = new_patch(sineSeed, envelopeSeed);
  fresh.seconds = 3.0;
  fresh.score = default_embedded_score();
  take(s, OpenPatch{std::move(fresh), {}, History{}});
}

void save_file(Session& s) {
  if (s.open.path.empty()) {
    s.status = "the patch has no file yet: save it as one";
    return;
  }
  try {
    save(s.open);
    s.gesture.clear();                             // the next edit is a step of its own, so it shows as unsaved
    note_opened(s.recent, s.open.path);
  } catch (const std::exception& e) {
    s.status = e.what();
  }
}

void save_file_as(Session& s, const std::string& path) {
  try {
    save_as(s.open, path);
    s.gesture.clear();
    note_opened(s.recent, path);
  } catch (const std::exception& e) {
    s.status = e.what();
  }
}

std::string title(const Session& s) {
  if (s.open.path.empty() && s.open.patch.graph.nodes.empty() && !unsaved(s.open)) return "MForce";
  const std::string name =
      s.open.path.empty() ? std::string("untitled") : std::filesystem::path(s.open.path).filename().string();
  return "MForce - " + name + (unsaved(s.open) ? " *" : "");
}

}  // namespace mforce::ui
