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
  next->set_wheel(wheel_);                        // before the swap: the first buffer has them
  next->set_pressure(pressure_);
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

// The keys below `limit` other than the line's go first, with no carry back
// (on an instrument that sustains such a key holds no note of its own); the
// line's key, when it is one of them, goes last, by key_up, which carries the
// line back to a key still down or releases it.
void LivePlay::all_keys_up_below(int limit) {
  const int line = lineKey_;
  for (auto it = down_.begin(); it != down_.end();) {
    if (it->key >= limit || it->key == line) {
      ++it;
      continue;
    }
    if (player_ && it->id != 0 && !sustaining_) player_->note_off(it->id);
    it = down_.erase(it);
  }
  if (line >= 0 && line < limit) key_up(line);
}

void LivePlay::set_wheel(float v) {
  wheel_ = v;
  if (player_) player_->set_wheel(v);
}

void LivePlay::set_pressure(float v) {
  pressure_ = v;
  if (player_) player_->set_pressure(v);
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
