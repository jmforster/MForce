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

// --- control thread -------------------------------------------------------

int LivePlayer::sample_rate() const { return sampleRate_; }

void LivePlayer::set_wheel(float v) {
  instrument_->instrument_state()->wheel.store(v, std::memory_order_relaxed);
}

void LivePlayer::set_pressure(float v) {
  instrument_->instrument_state()->pressure.store(v, std::memory_order_relaxed);
}

void LivePlayer::set_volume(float v) { volume_.store(v, std::memory_order_relaxed); }

int LivePlayer::sounding() {
  drain_done();
  return int(std::count_if(handed_.begin(), handed_.end(), [](const Handed& h) { return h.out; }));
}

// A voice given back is idle again, its envelopes ungated as a voice that
// played no held line leaves them.
void LivePlayer::drain_done() {
  Done d;
  while (toControl_.pop(d)) {
    instrument_->pool().voice(d.voice).ungate();
    handed_[size_t(d.voice)] = Handed{};
  }
}

int LivePlayer::idle_voice() const {
  for (size_t v = 0; v < handed_.size(); ++v)
    if (!handed_[v].out) return int(v);
  return -1;
}

// When as many voices sound, not fading, as the player may sound, one is
// stolen: the released voice that started longest ago, else the voice that
// started longest ago. It fades; a stolen line is closed. False when the
// fade cannot be sent.
bool LivePlayer::steal_if_full() {
  int sounding = 0, victim = -1;
  for (size_t v = 0; v < handed_.size(); ++v) {
    const Handed& h = handed_[v];
    if (!h.out || h.fading) continue;
    ++sounding;
    if (victim < 0) { victim = int(v); continue; }
    const Handed& best = handed_[size_t(victim)];
    if ((h.released && !best.released) || (h.released == best.released && h.order < best.order)) victim = int(v);
  }
  if (sounding < voices_) return true;
  Message m;
  m.kind = Message::Kind::Fade;
  m.voice = victim;
  if (!toAudio_.push(m)) return false;
  handed_[size_t(victim)].fading = true;
  if (line_.open && line_.voice == victim) line_ = Line{};
  return true;
}

LivePlayer::NoteId LivePlayer::note_on(const PerformedNote& note) {
  drain_done();
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
  m.id = nextId_;
  m.note = n;
  m.note.curve = nullptr;
  m.hold = note.hold;
  m.fromFreq = line_.lastFreq;
  if (!toAudio_.push(m)) return 0;
  const NoteId id = nextId_++;
  Handed& h = handed_[size_t(line_.voice)];
  h.id = id;
  line_.id = id;
  line_.lastFreq = n.freq;
  if (!note.hold) {
    h.released = true;
    line_ = Line{};
  }
  return id;
}

// A fresh note on an idle voice, a voice stolen first when the player is
// full. The voice is started here, on the control side, then handed over.
LivePlayer::NoteId LivePlayer::start_fresh(const PerformedNote& note, const Voice::Note& n) {
  const int v = idle_voice();
  if (v < 0 || !steal_if_full()) return 0;
  Voice& voice = instrument_->pool().voice(v);
  const bool held = choose_delivery(false, note.hold, voice.can_hold()) == Delivery::OpenLine;
  voice.start(n, held);
  Message m;
  m.kind = Message::Kind::Start;
  m.voice = v;
  m.id = nextId_;
  m.gain = note.velocity;
  m.bodySamples = n.samples;
  m.held = held;
  if (!toAudio_.push(m)) {
    voice.ungate();
    return 0;
  }
  const NoteId id = nextId_++;
  handed_[size_t(v)] = Handed{true, id, nextOrder_++, !held, false};
  if (held) line_ = Line{true, v, id, n.freq};
  return id;
}

void LivePlayer::note_off(NoteId id) {
  drain_done();
  if (!line_.open || line_.id != id) return;
  Message m;
  m.kind = Message::Kind::Release;
  m.voice = line_.voice;
  m.id = id;
  if (!toAudio_.push(m)) return;
  handed_[size_t(line_.voice)].released = true;
  line_ = Line{};
}

// --- audio thread ---------------------------------------------------------

LivePlayer::Sounding* LivePlayer::find(int voice) noexcept {
  for (auto& s : sounding_)
    if (s.voice == voice) return &s;
  return nullptr;
}

// The gate closes against the ending note's length; the release's remainder
// is the rest of the body, then the ring-out.
void LivePlayer::release(Sounding& s, int lastNoteSamples) noexcept {
  const int rem = instrument_->pool().voice(s.voice).release(lastNoteSamples);
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
      instrument_->pool().voice(m.voice).carry_on(m.note, m.fromFreq, glideSec_);
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
  Voice& voice = instrument_->pool().voice(s.voice);
  for (int i = 0; i < frames; ++i) {
    if (s.releaseAt >= 0 && s.noteSamples == s.releaseAt) release(s, s.releaseAt);
    if (s.fadeLeft == 0 || !s.ring.more()) { s.over = true; return; }
    float x = s.ring.shape(voice.next() * s.gain);
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
