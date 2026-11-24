#include "mforce/ui_control/live_play.h"
#include <algorithm>
#include <exception>
#include <memory>
#include "mforce/contract/performance.h"
#include "mforce/render/fault_report.h"
#include "mforce/ui_control/sounds.h"
#include "fault_line.h"

namespace mforce::ui {

namespace {

constexpr int kNotesAtOnce = 8;

}  // namespace

LivePlay::LivePlay(AudioAdapter& audio) : audio_(audio) {}

std::string LivePlay::rebuild(const Patch& patch) {
  if (!patch.instrument) return "a plain sound has no instrument to play yet";
  Patch live = patch;
  live.sampleRate = audio_.sample_rate();         // live play is at the output's rate
  std::unique_ptr<KeysSound> sound;
  try {
    sound = std::make_unique<KeysSound>(std::make_unique<LivePlayer>(live, kNotesAtOnce, false));
  } catch (const std::exception& e) {
    return e.what();
  }
  LivePlayer* next = &sound->player();
  player_ = nullptr;                              // dropped before the swap
  audio_.play(AudioAdapter::Line::Keys, std::move(sound));
  player_ = next;
  down_.clear();
  lineKey_ = -1;
  sustaining_ = patch.instrument->sustaining;
  return {};
}

void LivePlay::silence() {
  player_ = nullptr;
  audio_.play(AudioAdapter::Line::Keys, nullptr);
  down_.clear();
  lineKey_ = -1;
}

LivePlay::Down* LivePlay::find(int key) {
  for (Down& d : down_)
    if (d.key == key) return &d;
  return nullptr;
}

bool LivePlay::key_down(int key, int noteNumber, const Keyboard& how, float velocity, bool live) {
  if (!player_ || noteNumber < 0 || find(key)) return false;
  PerformedNote note;
  note.duration = how.seconds;
  note.noteNumber = float(noteNumber);
  note.velocity = velocity;
  note.hold = live;
  const LivePlayer::NoteId id = player_->note_on(note);
  if (id == 0) return false;
  down_.push_back(Down{key, noteNumber, velocity, how.seconds, live ? id : LivePlayer::NoteId{0}});
  if (live && sustaining_) lineKey_ = key;
  return true;
}

// The line carried to `to`'s note, held, at its own velocity: the player
// gives a carried note that names no onset the instrument's "slur". False,
// and nothing changed, when the player refused the note.
bool LivePlay::carry_back(const Down& to) {
  PerformedNote note;
  note.duration = to.seconds;
  note.noteNumber = float(to.noteNumber);
  note.velocity = to.velocity;
  note.hold = true;
  const LivePlayer::NoteId id = player_->note_on(note);
  if (id == 0) return false;
  for (Down& d : down_)
    if (d.key == to.key) d.id = id;
  lineKey_ = to.key;
  return true;
}

void LivePlay::key_up(int key) {
  auto it = std::find_if(down_.begin(), down_.end(), [key](const Down& d) { return d.key == key; });
  if (it == down_.end()) return;
  const Down up = *it;
  down_.erase(it);
  if (!player_ || up.id == 0) return;
  if (!sustaining_) {
    player_->note_off(up.id);
    return;
  }
  if (key != lineKey_) return;                    // a note the line has moved on from: forgotten
  const auto back = std::find_if(down_.rbegin(), down_.rend(), [](const Down& d) { return d.id != 0; });
  if (back != down_.rend()) {
    if (carry_back(*back)) return;
    // Refused (the player's queue is full, or a key-up or stop waits): the
    // line is released instead, which the player keeps until it can send it,
    // and no key down holds a note of it any more.
    for (Down& d : down_) d.id = 0;
  }
  player_->note_off(up.id);
  lineKey_ = -1;
}

// Every key up at once: each held note released, the line with no carry back.
void LivePlay::all_keys_up() {
  if (player_)
    for (const Down& d : down_)
      if (d.id != 0 && (!sustaining_ || d.key == lineKey_)) player_->note_off(d.id);
  down_.clear();
  lineKey_ = -1;
}

std::vector<int> LivePlay::held_notes() const {
  std::vector<int> notes;
  for (const Down& d : down_) notes.push_back(d.noteNumber);
  std::sort(notes.begin(), notes.end());
  return notes;
}

void LivePlay::stop() {
  if (player_) player_->stop();
  for (Down& d : down_) d.id = 0;
  lineKey_ = -1;
}

int LivePlay::sounding_notes(std::span<int> into) { return player_ ? player_->sounding_notes(into) : 0; }

int LivePlay::sounding() { return player_ ? player_->sounding() : 0; }

std::string LivePlay::faults() {
  if (!player_) return {};
  std::vector<FaultReport> reports;
  player_->collect_faults(reports);
  return fault_line(reports);
}

}  // namespace mforce::ui
