#include "mforce/ui_control/live_play.h"
#include <algorithm>
#include <exception>
#include <memory>
#include "mforce/contract/performance.h"

namespace mforce::ui {

namespace {

constexpr int kNotesAtOnce = 8;

}  // namespace

LivePlay::LivePlay(AudioAdapter& audio) : audio_(audio) {}

std::string LivePlay::rebuild(const Patch& patch) {
  if (!patch.instrument) return "a plain sound has no instrument to play yet";
  Patch live = patch;
  live.sampleRate = audio_.sample_rate();         // live play is at the output's rate
  std::unique_ptr<LivePlayer> player;
  try {
    player = std::make_unique<LivePlayer>(live, kNotesAtOnce);
  } catch (const std::exception& e) {
    return e.what();
  }
  audio_.play(std::move(player));
  down_.clear();
  sustaining_ = patch.instrument->sustaining;
  return {};
}

std::string LivePlay::value_changed(const Patch& patch, std::string_view node, std::string_view pin, float v) {
  LivePlayer* player = audio_.player();
  if (player && player->set_value(node, pin, v)) return {};
  return rebuild(patch);
}

bool LivePlay::playing() const { return audio_.player() != nullptr; }

void LivePlay::key_down(int key, int noteNumber, const Keyboard& how) {
  LivePlayer* player = audio_.player();
  if (!player || noteNumber < 0 || down_.count(key)) return;
  PerformedNote note;
  note.duration = how.seconds;
  note.noteNumber = float(noteNumber);
  note.velocity = how.velocity;
  note.hold = sustaining_;
  const LivePlayer::NoteId id = player->note_on(note);
  if (id != 0) down_[key] = Down{id, noteNumber};
}

void LivePlay::key_up(int key) {
  auto it = down_.find(key);
  if (it == down_.end()) return;
  if (sustaining_)
    if (LivePlayer* player = audio_.player()) player->note_off(it->second.id);
  down_.erase(it);
}

void LivePlay::all_keys_up() {
  while (!down_.empty()) key_up(down_.begin()->first);
}

std::vector<int> LivePlay::held_notes() const {
  std::vector<int> notes;
  for (const auto& [key, down] : down_) notes.push_back(down.noteNumber);
  std::sort(notes.begin(), notes.end());
  return notes;
}

}  // namespace mforce::ui
