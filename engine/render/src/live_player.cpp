#include "mforce/render/live_player.h"
#include "mforce/core/equal_temperament.h"
#include "mforce/render/build_instrument.h"
#include "mforce/render/delivery.h"
#include <algorithm>

namespace mforce {

LivePlayer::LivePlayer(const Patch& patch, int voices) {
  const int polyphony = patch.instrument ? patch.instrument->polyphony : 0;
  voices_ = std::max({voices, polyphony, 1});
  instrument_ = build_instrument(patch, 2 * voices_);
  sampleRate_ = instrument_->render_context().sampleRate;
  fadeSamples_ = int(0.005f * float(sampleRate_));
  glideSec_ = instrument_->glide_seconds();
  volume_.store(instrument_->volume, std::memory_order_relaxed);
  handed_.assign(size_t(instrument_->pool().size()), Handed{});
  sounding_.reserve(size_t(instrument_->pool().size()));
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

// Every control call begins here: the voices given back are idle again,
// their envelopes ungated as a voice that played no held line leaves them;
// then a Release that could not be queued is sent.
void LivePlayer::catch_up() {
  Done d;
  while (toControl_.pop(d)) {
    voice(d.voice).ungate();
    handed_[size_t(d.voice)] = Handed{};
  }
  if (pendingRelease_ >= 0 && toAudio_.push(release_message(pendingRelease_))) pendingRelease_ = -1;
}

LivePlayer::Message LivePlayer::release_message(int v) const {
  Message m;
  m.kind = Message::Kind::Release;
  m.voice = v;
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

LivePlayer::NoteId LivePlayer::note_on(const PerformedNote& note) {
  catch_up();
  if (pendingRelease_ >= 0) return 0;
  const Voice::Note n{note_to_freq(note.noteNumber), note.velocity,
                      int(note.duration * float(sampleRate_)), note.duration, note.onset,
                      note.curve ? &*note.curve : nullptr};
  if (line_.open) return carry(note, n);
  return start_fresh(note, n);
}

// The next note of the open line: a fresh id, carried onto the line's voice.
// A note that does not hold closes the line.
LivePlayer::NoteId LivePlayer::carry(const PerformedNote& note, const Voice::Note& n) {
  Message m;
  m.kind = Message::Kind::Carry;
  m.voice = line_.voice;
  m.note = n;
  m.note.curve = nullptr;
  m.hold = note.hold;
  m.fromFreq = line_.lastFreq;
  if (!toAudio_.push(m)) return 0;
  const NoteId id = take_id();
  Handed& h = handed_[size_t(line_.voice)];
  h.id = id;
  line_.id = id;
  line_.lastFreq = n.freq;
  if (!note.hold) {
    h.released = true;
    h.releasedAt = nextOrder_++;
    line_ = Line{};
  }
  return id;
}

// A fresh note on an idle voice, a voice stolen first when the player is
// full. Nothing is sent unless the queue has room for all of it. The voice is
// started here, on the control side, then handed over.
LivePlayer::NoteId LivePlayer::start_fresh(const PerformedNote& note, const Voice::Note& n) {
  const int v = idle_voice();
  if (v < 0) return 0;
  const int victim = steal_victim();
  if (toAudio_.space() < (victim >= 0 ? 2u : 1u)) return 0;
  if (victim >= 0) {
    Message fade;
    fade.kind = Message::Kind::Fade;
    fade.voice = victim;
    (void)toAudio_.push(fade);
    handed_[size_t(victim)].fading = true;
  }
  Voice& started = voice(v);
  const bool held = choose_delivery(false, note.hold, started.can_hold()) == Delivery::OpenLine;
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
  handed_[size_t(v)] = Handed{true, id, order, order, !held, false};
  lastStarted_ = v;
  if (held) line_ = Line{true, v, id, n.freq};
  return id;
}

// The line is closed here whatever happens; a Release the queue cannot take
// now waits and goes first at the next control call.
void LivePlayer::note_off(NoteId id) {
  catch_up();
  if (!line_.open || line_.id != id) return;
  Handed& h = handed_[size_t(line_.voice)];
  h.released = true;
  h.releasedAt = nextOrder_++;
  if (!toAudio_.push(release_message(line_.voice))) pendingRelease_ = line_.voice;
  line_ = Line{};
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
  if (m.kind == Message::Kind::Start) {
    Sounding s{m.voice, m.gain, m.held ? RingOut(sampleRate_) : RingOut(sampleRate_, m.bodySamples)};
    sounding_.push_back(s);
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
    case Message::Kind::Fade:    s->fadeLeft = fadeSamples_; break;
    case Message::Kind::Start:   break;
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
      x *= float(s.fadeLeft) / float(fadeSamples_);
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
  apply_output_stage(mono, frames, volume_.load(std::memory_order_relaxed), instrument_->peakGuard);
}

} // namespace mforce
