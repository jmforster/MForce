#include "mforce/render/live_player.h"
#include "mforce/core/equal_temperament.h"
#include "mforce/render/build_instrument.h"
#include "mforce/render/delivery.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace mforce {

LivePlayer::LivePlayer(const Patch& patch, int voices, bool peakGuard) : peakGuard_(peakGuard) {
  const int polyphony = patch.instrument ? patch.instrument->polyphony : 0;
  voices_ = std::max({voices, polyphony, 1});
  instrument_ = build_instrument(patch, 2 * voices_);
  sampleRate_ = instrument_->render_context().sampleRate;
  fadeSamples_ = int(0.005f * float(sampleRate_));
  cutSamples_ = std::max(1, int(0.003f * float(sampleRate_)));
  glideSec_ = instrument_->glide_seconds();
  sustains_ = instrument_->instrument_class().sustaining;
  slurOnset_ = instrument_->instrument_class().onset_id("slur");
  volume_.store(instrument_->volume, std::memory_order_relaxed);
  handed_.assign(size_t(instrument_->pool().size()), Handed{});
  releaseWaits_.assign(size_t(instrument_->pool().size()), 0);
  sounding_.reserve(size_t(instrument_->pool().size()));
  check_constants_agree();
  const size_t constants = instrument_->pool().size() > 0 ? voice(0).constants().size() : 0;
  kept_.assign(constants, 0.0f);
  isKept_.assign(constants, 0);
  unsent_.assign(constants, 0);
}

// The player writes constant k of every voice as one: that holds only when
// the voices, built from one graph, list the same node and pin at each index.
void LivePlayer::check_constants_agree() {
  const int n = int(instrument_->pool().size());
  if (n == 0) return;
  const auto first = voice(0).constants();
  for (int v = 1; v < n; ++v) {
    const auto other = voice(v).constants();
    bool same = other.size() == first.size();
    for (size_t k = 0; same && k < first.size(); ++k)
      same = other[k].node == first[k].node && other[k].pin == first[k].pin;
    if (!same)
      throw std::runtime_error("the live player: voice " + std::to_string(v) +
                               " lists its constants unlike voice 0, so a value cannot be set in every voice");
  }
}

Voice& LivePlayer::voice(int v) { return instrument_->pool_.voice(v); }

// --- control thread -------------------------------------------------------

int LivePlayer::sample_rate() const { return sampleRate_; }

void LivePlayer::set_wheel(float v) {
  catch_up();
  instrument_->instrument_state()->wheel.store(v, std::memory_order_relaxed);
}

void LivePlayer::set_pressure(float v) {
  catch_up();
  instrument_->instrument_state()->pressure.store(v, std::memory_order_relaxed);
}

void LivePlayer::set_volume(float v) {
  catch_up();
  volume_.store(v, std::memory_order_relaxed);
}

int LivePlayer::sounding() {
  catch_up();
  return int(std::count_if(handed_.begin(), handed_.end(), [](const Handed& h) { return h.out; }));
}

// Each note is placed among the lowest kept so far: `into` stays sorted and
// holds the lowest of the notes seen.
int LivePlayer::sounding_notes(std::span<int> into) {
  catch_up();
  const size_t room = into.size();
  size_t count = 0;
  for (const Handed& h : handed_) {
    if (!h.out) continue;
    if (count == room && (room == 0 || h.note >= into[room - 1])) continue;
    size_t at = count < room ? count++ : room - 1;
    for (; at > 0 && into[at - 1] > h.note; --at) into[at] = into[at - 1];
    into[at] = h.note;
  }
  return int(count);
}

void LivePlayer::collect_faults(std::vector<FaultReport>& out) {
  catch_up();
  instrument_->collect_faults(out);
}

// Every control call begins here: the voices given back are idle again,
// their envelopes ungated as a voice that played no held line leaves them,
// and a key-up still waiting for one of them is moot; then a stop that could
// not be queued is sent, then the key-ups, then the Sets.
void LivePlayer::catch_up() {
  Done d;
  while (toControl_.pop(d)) {
    voice(d.voice).ungate();
    handed_[size_t(d.voice)] = Handed{};
    if (releaseWaits_[size_t(d.voice)]) {
      releaseWaits_[size_t(d.voice)] = 0;
      --waitingReleases_;
    }
  }
  if (stopWaits_) {
    Message m;
    m.kind = Message::Kind::Stop;
    if (toAudio_.push(m)) stopWaits_ = false;
  }
  if (!stopWaits_ && waitingReleases_ > 0) send_releases();
  if (unsentCount_ > 0) send_unsent();
}

bool LivePlayer::waiting() const { return stopWaits_ || waitingReleases_ > 0; }

// The key-ups that found no room, in voice order, until the queue is full.
void LivePlayer::send_releases() {
  for (size_t v = 0; v < releaseWaits_.size() && waitingReleases_ > 0; ++v) {
    if (!releaseWaits_[v]) continue;
    if (!toAudio_.push(release_message(int(v)))) return;
    releaseWaits_[v] = 0;
    --waitingReleases_;
  }
}

// The index of the constant of `pin` on `node` in every voice's constants();
// -1 when the voices have none.
int LivePlayer::constant_index(std::string_view node, std::string_view pin) {
  if (instrument_->pool().size() == 0) return -1;
  const auto list = voice(0).constants();
  for (size_t k = 0; k < list.size(); ++k)
    if (list[k].node == node && list[k].pin == pin) return int(k);
  return -1;
}

LivePlayer::Message LivePlayer::set_message(int k) const {
  Message m;
  m.kind = Message::Kind::Set;
  m.constant = k;
  m.value = kept_[size_t(k)];
  return m;
}

// The Sets that found no room, each with its constant's latest value, in
// index order, until the queue is full again.
void LivePlayer::send_unsent() {
  for (size_t k = 0; k < unsent_.size() && unsentCount_ > 0; ++k) {
    if (!unsent_[k]) continue;
    if (!toAudio_.push(set_message(int(k)))) return;
    unsent_[k] = 0;
    --unsentCount_;
  }
}

// Every value set so far, into a voice the control thread holds.
void LivePlayer::write_kept(Voice& v) {
  const auto list = v.constants();
  for (size_t k = 0; k < list.size(); ++k)
    if (isKept_[k]) list[k].set(kept_[k]);
}

bool LivePlayer::set_value(std::string_view node, std::string_view pin, float v) {
  catch_up();
  const int k = constant_index(node, pin);
  if (k < 0) return false;
  const size_t i = size_t(k);
  kept_[i] = v;
  isKept_[i] = 1;
  const bool sent = toAudio_.push(set_message(k));
  if (sent && unsent_[i]) {
    unsent_[i] = 0;
    --unsentCount_;
  } else if (!sent && !unsent_[i]) {
    unsent_[i] = 1;
    ++unsentCount_;
  }
  return true;
}

LivePlayer::Message LivePlayer::release_message(int v) const {
  Message m;
  m.kind = Message::Kind::Release;
  m.voice = v;
  return m;
}

LivePlayer::Message LivePlayer::fade_message(int v, int samples) const {
  Message m;
  m.kind = Message::Kind::Fade;
  m.voice = v;
  m.fadeSamples = samples;
  return m;
}

LivePlayer::NoteId LivePlayer::take_id() {
  const NoteId id = nextId_++;
  if (nextId_ == 0) nextId_ = 1;   // 0 means "not sent"
  return id;
}

// The idle voice after the one last started, in rotation, so a new note
// keeps off the voice just used; -1 when none is idle.
int LivePlayer::idle_voice() const {
  const int n = int(handed_.size());
  for (int k = 1; k <= n; ++k) {
    const int v = (lastStarted_ + k) % n;
    if (!handed_[size_t(v)].out) return v;
  }
  return -1;
}

// When as many voices sound, not fading, as the player may sound: the voice
// released longest ago, else the voice started longest ago. -1 when the
// player is not full.
int LivePlayer::steal_victim() const {
  int sounding = 0, victim = -1;
  for (size_t v = 0; v < handed_.size(); ++v) {
    const Handed& h = handed_[v];
    if (!h.out || h.fading) continue;
    ++sounding;
    if (victim < 0) { victim = int(v); continue; }
    const Handed& best = handed_[size_t(victim)];
    const bool earlier = h.released ? (!best.released || h.releasedAt < best.releasedAt)
                                    : (!best.released && h.startedAt < best.startedAt);
    if (earlier) victim = int(v);
  }
  return sounding < voices_ ? -1 : victim;
}

// The voices a line opening now cuts: every voice handed over, not fading.
int LivePlayer::to_cut() const {
  return int(std::count_if(handed_.begin(), handed_.end(), [](const Handed& h) { return h.out && !h.fading; }));
}

// A fade the queue has room for: the caller checked.
void LivePlayer::fade_out(int v, int samples) {
  (void)toAudio_.push(fade_message(v, samples));
  handed_[size_t(v)].fading = true;
}

LivePlayer::NoteId LivePlayer::note_on(const PerformedNote& note) {
  catch_up();
  if (waiting()) return 0;
  const Voice::Note n{note_to_freq(note.noteNumber), note.velocity,
                      int(note.duration * float(sampleRate_)), note.duration, note.onset,
                      note.curve ? &*note.curve : nullptr};
  if (line_.open) return carry(note, n);
  return start_fresh(note, n);
}

// The next note of the open line: a fresh id, carried onto the line's voice,
// with "slur" as its onset when it names none. A note that does not hold
// closes the line.
LivePlayer::NoteId LivePlayer::carry(const PerformedNote& note, const Voice::Note& n) {
  Message m;
  m.kind = Message::Kind::Carry;
  m.voice = line_.voice;
  m.note = n;
  m.note.curve = nullptr;
  if (m.note.onset == 0) m.note.onset = slurOnset_;
  m.hold = note.hold;
  m.fromFreq = line_.lastFreq;
  if (!toAudio_.push(m)) return 0;
  const NoteId id = take_id();
  Handed& h = handed_[size_t(line_.voice)];
  h.id = id;
  h.note = int(std::lround(note.noteNumber));
  line_.id = id;
  line_.lastFreq = n.freq;
  if (!note.hold) {
    h.released = true;
    h.releasedAt = nextOrder_++;
    line_ = Line{};
  }
  return id;
}

// A fresh note on an idle voice. A held note opening the line first cuts
// every other voice handed over; otherwise a voice is stolen first when the
// player is full. Nothing is sent unless the queue has room for all of it.
// The voice is started here, on the control side, then handed over.
LivePlayer::NoteId LivePlayer::start_fresh(const PerformedNote& note, const Voice::Note& n) {
  const int v = idle_voice();
  if (v < 0) return 0;
  Voice& started = voice(v);
  const bool held = choose_delivery(false, note.hold, started.can_hold()) == Delivery::OpenLine;
  const bool opensLine = held && sustains_;
  const int victim = opensLine ? -1 : steal_victim();
  const size_t fades = opensLine ? size_t(to_cut()) : (victim >= 0 ? 1u : 0u);
  if (toAudio_.space() < fades + 1) return 0;
  if (opensLine) {
    for (size_t k = 0; k < handed_.size(); ++k)
      if (handed_[k].out && !handed_[k].fading) fade_out(int(k), cutSamples_);
  } else if (victim >= 0) {
    fade_out(victim, fadeSamples_);
  }
  write_kept(started);
  started.start(n, held);
  Message m;
  m.kind = Message::Kind::Start;
  m.voice = v;
  m.gain = note.velocity;
  m.bodySamples = n.samples;
  m.held = held;
  (void)toAudio_.push(m);
  const NoteId id = take_id();
  const std::uint64_t order = nextOrder_++;
  handed_[size_t(v)] = Handed{true, id, int(std::lround(note.noteNumber)), order, order, !held, false};
  lastStarted_ = v;
  if (opensLine) line_ = Line{true, v, id, n.freq};
  return id;
}

// The voice holding `id`, released: the line closes when it is the line's.
// A Release the queue cannot take now waits and goes at a later control call.
void LivePlayer::note_off(NoteId id) {
  catch_up();
  if (id == 0) return;
  for (size_t v = 0; v < handed_.size(); ++v) {
    Handed& h = handed_[v];
    if (!h.out || h.released || h.fading || h.id != id) continue;
    h.released = true;
    h.releasedAt = nextOrder_++;
    if (line_.open && line_.voice == int(v)) line_ = Line{};
    if (stopWaits_ || !toAudio_.push(release_message(int(v)))) {
      releaseWaits_[v] = 1;
      ++waitingReleases_;
    }
    return;
  }
}

// Every voice handed over is marked fading; the audio thread fades each it
// holds, in queue order, so a note started before this fades too.
void LivePlayer::stop() {
  catch_up();
  line_ = Line{};
  std::fill(releaseWaits_.begin(), releaseWaits_.end(), std::uint8_t{0});
  waitingReleases_ = 0;
  for (Handed& h : handed_)
    if (h.out) {
      h.fading = true;
      if (!h.released) {
        h.released = true;
        h.releasedAt = nextOrder_++;
      }
    }
  Message m;
  m.kind = Message::Kind::Stop;
  stopWaits_ = !toAudio_.push(m);
}

// --- audio thread ---------------------------------------------------------

LivePlayer::Sounding* LivePlayer::find(int v) noexcept {
  for (auto& s : sounding_)
    if (s.voice == v) return &s;
  return nullptr;
}

// The gate closes against the ending note's length; the release's remainder
// is the rest of the body, then the ring-out.
void LivePlayer::release(Sounding& s, int lastNoteSamples) noexcept {
  const int rem = voice(s.voice).release(lastNoteSamples);
  s.ring.set_body(s.played + rem);
  s.releaseAt = -1;
}

void LivePlayer::apply(const Message& m) noexcept {
  if (m.kind == Message::Kind::Set) {
    for (auto& s : sounding_) voice(s.voice).constants()[size_t(m.constant)].set(m.value);
    return;
  }
  if (m.kind == Message::Kind::Start) {
    Sounding s{m.voice, m.gain, m.held ? RingOut(sampleRate_) : RingOut(sampleRate_, m.bodySamples)};
    sounding_.push_back(s);
    return;
  }
  if (m.kind == Message::Kind::Stop) {
    for (auto& s : sounding_)
      if (!s.over && s.fadeLeft < 0) { s.fadeLeft = fadeSamples_; s.fadeLength = fadeSamples_; }
    return;
  }
  Sounding* s = find(m.voice);
  if (!s || s->over) return;
  switch (m.kind) {
    case Message::Kind::Carry:
      voice(m.voice).carry_on(m.note, m.fromFreq, glideSec_);
      s->noteSamples = 0;
      if (!m.hold) s->releaseAt = m.note.samples;
      break;
    case Message::Kind::Release: release(*s, s->noteSamples); break;
    case Message::Kind::Fade:
      if (s->fadeLeft < 0) { s->fadeLeft = m.fadeSamples; s->fadeLength = m.fadeSamples; }
      break;
    case Message::Kind::Start:
    case Message::Kind::Set:
    case Message::Kind::Stop:    break;
  }
}

// One voice summed into the buffer: per sample, the voice times its gain,
// the ring-out's shaping, the fade when fading, then the sum.
void LivePlayer::render(Sounding& s, float* mono, int frames) noexcept {
  Voice& v = voice(s.voice);
  for (int i = 0; i < frames; ++i) {
    if (s.releaseAt >= 0 && s.noteSamples == s.releaseAt) release(s, s.releaseAt);
    if (s.fadeLeft == 0 || !s.ring.more()) { s.over = true; return; }
    float x = s.ring.shape(v.next() * s.gain);
    if (s.fadeLeft > 0) {
      x *= float(s.fadeLeft) / float(s.fadeLength);
      --s.fadeLeft;
    }
    mono[i] += x;
    ++s.played;
    ++s.noteSamples;
  }
}

// Each voice that is over goes back; one the queue cannot take stays parked
// and is offered again next buffer.
void LivePlayer::give_back() noexcept {
  for (size_t k = 0; k < sounding_.size();) {
    if (sounding_[k].over && toControl_.push(Done{sounding_[k].voice})) {
      sounding_.erase(sounding_.begin() + std::ptrdiff_t(k));
      continue;
    }
    ++k;
  }
}

void LivePlayer::fill(float* mono, int frames) noexcept {
  Message m;
  while (toAudio_.pop(m)) apply(m);
  std::fill(mono, mono + frames, 0.0f);
  for (auto& s : sounding_)
    if (!s.over) render(s, mono, frames);
  give_back();
  apply_output_stage(mono, frames, volume_.load(std::memory_order_relaxed), peakGuard_);
}

} // namespace mforce
