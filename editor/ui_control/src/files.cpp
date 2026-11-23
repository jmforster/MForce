#include "mforce/ui_control/files.h"
#include <exception>
#include <filesystem>
#include <string>
#include <utility>
#include "mforce/ui_control/session.h"
#include "mforce/ui_model/placement.h"

namespace mforce::ui {

namespace {

// No player: the keys let go and the adapter handed silence, so the sound of
// the patch before this one stops.
void silence(Session& s) {
  s.live.all_keys_up();
  s.audio.play(nullptr);
}

// A player for the open patch, or silence; what is worth telling about it.
std::string start_sound(Session& s) {
  const Patch& p = s.open.patch;
  if (!p.instrument) {
    silence(s);
    return "a plain sound: it opens, edits and saves, and has no instrument to play yet";
  }
  const std::string refused = s.live.rebuild(p);
  if (!refused.empty()) {
    silence(s);
    return refused;
  }
  if (p.sampleRate != s.audio.sample_rate())
    return "the file is at " + std::to_string(p.sampleRate) + " Hz and plays here at " +
           std::to_string(s.audio.sample_rate()) + " Hz, the output's rate; a render uses the file's " +
           std::to_string(p.sampleRate) + " Hz";
  return {};
}

}  // namespace

void open_file(Session& s, const std::string& path) {
  OpenPatch next;
  try {
    next = open_patch(path);
  } catch (const std::exception& e) {
    s.status = e.what();
    return;
  }
  (void)place_unplaced_nodes(next.patch);
  s.open = std::move(next);
  s.selected.clear();
  s.unsaved = Unsaved{};
  ++s.opened;
  note_opened(s.recent, path);
  s.status = start_sound(s);
}

void save_file(Session& s) {
  if (s.open.path.empty()) {
    s.status = "the patch has no file yet: save it as one";
    return;
  }
  try {
    save(s.open);
  } catch (const std::exception& e) {
    s.status = e.what();
  }
}

void save_file_as(Session& s, const std::string& path) {
  try {
    save_as(s.open, path);
  } catch (const std::exception& e) {
    s.status = e.what();
  }
}

std::string title(const Session& s) {
  if (s.open.path.empty() && s.open.patch.graph.nodes.empty() && !s.open.dirty) return "MForce";
  const std::string name =
      s.open.path.empty() ? std::string("untitled") : std::filesystem::path(s.open.path).filename().string();
  return "MForce - " + name + (s.open.dirty ? " *" : "");
}

}  // namespace mforce::ui
