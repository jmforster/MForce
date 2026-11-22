#include "mforce/render/pitched_instrument.h"
#include "mforce/core/equal_temperament.h"
#include "mforce/render/delivery.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace mforce {

namespace {

// The peak of the last millisecond of `buf`: a voice is contained when it is
// at the audibility floor by its end.
float last_ms_peak(const std::vector<float>& buf, int sampleRate) {
  const int rendered = int(buf.size());
  const int checkStart = std::max(0, rendered - sampleRate / 1000);
  float tailPeak = 0.0f;
  for (int i = checkStart; i < rendered; ++i)
    tailPeak = std::max(tailPeak, std::fabs(buf[size_t(i)]));
  return tailPeak;
}

} // namespace

PitchedInstrument::PitchedInstrument(InstrumentClass cls, float glideSec)
    : class_(std::move(cls)), glideSec_(glideSec) {}

void PitchedInstrument::prepare(const RenderContext& c) {
  Instrument::prepare(c);
  for (auto& v : pool_) v.prepare(c);
}

void PitchedInstrument::collect_faults(std::vector<FaultReport>& out) {
  const auto first = std::ptrdiff_t(out.size());
  for (auto& v : pool_)
    for (auto& s : v.signals()) {
      const unsigned char f = s.node->take_faults();
      if (!f) continue;
      auto it = std::find_if(out.begin() + first, out.end(),
                             [&](const FaultReport& r) { return r.nodeId == s.id; });
      if (it == out.end()) out.push_back({s.id, s.node->type_name(), f});
      else it->faults |= f;
    }
  std::sort(out.begin() + first, out.end(),
            [](const FaultReport& a, const FaultReport& b) { return a.nodeId < b.nodeId; });
}

void PitchedInstrument::play(const Performance& part) {
  for (const auto& note : part.notes) play_note(note);
  if (line_.open) finish_line();
}

// The note's voice and its delivery: an open line takes it; otherwise the
// next voice in turn, which opens a line when the note is held and the voice
// can hold one. A held note on a voice that cannot is said once and played
// as an ordinary note.
void PitchedInstrument::play_note(const PerformedNote& pn) {
  if (pool_.empty()) return;
  const Voice::Note note{note_to_freq(pn.noteNumber), pn.velocity,
                         int(pn.duration * float(render_context().sampleRate)), pn.duration,
                         pn.onset, pn.curve ? &*pn.curve : nullptr};
  const int v = line_.open ? line_.voice : pool_.next_scheduled();
  const bool canHold = pool_.voice(v).can_hold();
  if (!line_.open && pn.hold && !canHold && !warnedNoEnvelopes_) {
    warnedNoEnvelopes_ = true;
    std::fprintf(stderr, "[onset] hold requested but this voice has no "
                 "reachable envelopes (Multiplex output?) — the line is "
                 "delivered as ordinary notes\n");
  }
  switch (choose_delivery(line_.open, pn.hold, canHold)) {
    case Delivery::Continue: continue_line(pn, note); break;
    case Delivery::OpenLine: open_line(v, pn, note); break;
    case Delivery::Classic:  play_classic(v, pn, note); break;
  }
}

// `n` samples of voice `v`, at `gain`, appended to `buf`; with a capture
// active, each sample is recorded at its timeline frame.
void PitchedInstrument::render_into(int v, std::vector<float>& buf, int startFrame, int n, float gain) {
  Voice& voice = pool_.voice(v);
  const bool capturing = capture_.active();
  const size_t base = buf.size();
  buf.resize(base + size_t(n));
  for (int k = 0; k < n; ++k) {
    buf[base + size_t(k)] = voice.next() * gain;
    if (capturing) capture_.record(v, startFrame + int(base) + k);
  }
}

// The voice played on, a sample at a time, for as long as `ring` says.
void PitchedInstrument::ring_out(int v, std::vector<float>& buf, int startFrame, float gain, RingOut& ring) {
  while (ring.more()) {
    render_into(v, buf, startFrame, 1, gain);
    buf.back() = ring.shape(buf.back());
  }
}

// The line's next note on its voice, rendered for its length; a note without
// `hold` ends the line.
void PitchedInstrument::continue_line(const PerformedNote& pn, const Voice::Note& note) {
  pool_.voice(line_.voice).carry_on(note, line_.lastFreq, glideSec_);
  line_.lastFreq = note.freq;
  line_.lastNote = pn.noteNumber;
  line_.lastSamples = note.samples;
  render_into(line_.voice, line_.buf, frame_of(line_.startTime), note.samples, line_.gain);
  if (!pn.hold) finish_line();
}

// A fresh held note: the voice started gated, the line opened on it and the
// note rendered for its length. The line's gain is this note's velocity:
// later notes reach the graph through the velocity adapter.
void PitchedInstrument::open_line(int v, const PerformedNote& pn, const Voice::Note& note) {
  pool_.voice(v).start(note, true);
  line_.open = true;
  line_.voice = v;
  line_.startTime = pn.start;
  line_.gain = pn.velocity;
  line_.lastFreq = note.freq;
  line_.lastNote = pn.noteNumber;
  line_.lastSamples = note.samples;
  line_.buf.clear();
  line_.buf.reserve(size_t(note.samples + int(RingOut::kMaxSeconds * float(render_context().sampleRate))));
  render_into(v, line_.buf, frame_of(pn.start), note.samples, pn.velocity);
}

// A fresh note, its release laid out inside its own length, rendered with
// the tail and the ring-out, checked for containment and played.
void PitchedInstrument::play_classic(int v, const PerformedNote& pn, const Voice::Note& note) {
  const int sampleRate = render_context().sampleRate;
  pool_.voice(v).start(note, false);
  RingOut ring(sampleRate, note.samples);
  std::vector<float> buf;
  buf.reserve(size_t(ring.limit()));
  ring_out(v, buf, frame_of(pn.start), pn.velocity, ring);
  if (const float tailPeak = last_ms_peak(buf, sampleRate); tailPeak > 1e-4f)
    std::fprintf(stderr,
        "[containment] note %.1f (%.1f Hz) at t=%.2fs: %.1f dBFS in final 1 ms\n",
        pn.noteNumber, note.freq, pn.start, 20.0f * std::log10(tailPeak));
  add_rendered(pn.start, buf.data(), int(buf.size()));
}

// The end of the held line: the gate closes and the release runs against the
// releasing note's length; the voice rings out with the follower primed over
// the whole line, is checked for containment once, and the line reaches the
// timeline as one block. The voice is handed back ungated.
void PitchedInstrument::finish_line() {
  const int sampleRate = render_context().sampleRate;
  Voice& voice = pool_.voice(line_.voice);
  const int rem = voice.release(line_.lastSamples);
  auto& buf = line_.buf;
  RingOut ring(sampleRate, int(buf.size()) + rem);
  buf.reserve(size_t(ring.limit()));
  ring.prime(buf);
  ring_out(line_.voice, buf, frame_of(line_.startTime), line_.gain, ring);
  if (const float tailPeak = last_ms_peak(buf, sampleRate); tailPeak > 1e-4f)
    std::fprintf(stderr,
        "[containment] line ending on note %.1f (%.1f Hz) at t=%.2fs: "
        "%.1f dBFS in final 1 ms\n",
        line_.lastNote, line_.lastFreq, line_.startTime, 20.0f * std::log10(tailPeak));
  add_rendered(line_.startTime, buf.data(), int(buf.size()));
  voice.ungate();
  line_ = HeldLine{};
}

// --- The live calls -------------------------------------------------------

PitchedInstrument::StreamingVoice PitchedInstrument::prepare_voice(float noteNumber, float velocity,
                                                                   float duration, const PitchCurve* curve) {
  return prepare_voice_at(pool_.next_scheduled(), noteNumber, velocity, duration, curve);
}

PitchedInstrument::StreamingVoice PitchedInstrument::prepare_voice_at(int slot, float noteNumber, float velocity,
                                                                      float duration, const PitchCurve* curve) {
  Voice& voice = pool_.voice(slot);
  const int sampleRate = render_context().sampleRate;
  const int durSamples = int(duration * float(sampleRate));
  voice.start_untriggered({note_to_freq(noteNumber), velocity, durSamples, duration, 0, curve});
  // The life length includes the fixed tail; the note itself is unextended.
  const int tailSamples = int(RingOut::kTailSeconds * float(sampleRate));
  return {voice.output(), durSamples + tailSamples, velocity * volume, voice.perform_source(),
          voice.tails(), voice.clock()};
}

int PitchedInstrument::acquire_voice() {
  const int n = pool_.size();
  if (n == 0) return -1;
  if (int(slotInUse_.size()) != n) slotInUse_.assign(size_t(n), 0);
  for (int k = 0; k < n; ++k) {
    const int s = (pool_.rotation() + k) % n;
    if (!slotInUse_[size_t(s)]) {
      slotInUse_[size_t(s)] = 1;
      pool_.set_rotation(s + 1);
      return s;
    }
  }
  return -1;
}

void PitchedInstrument::release_voice(int slot) {
  if (slot >= 0 && slot < int(slotInUse_.size())) slotInUse_[size_t(slot)] = 0;
}

void PitchedInstrument::release_all_voices() { slotInUse_.assign(size_t(pool_.size()), 0); }

void PitchedInstrument::continue_voice_live(int slot, float noteNumber, float velocity, float duration,
                                            float fromFreq, float onsetId) {
  if (slot < 0 || slot >= pool_.size()) return;
  const int durSamples = int(duration * float(render_context().sampleRate));
  pool_.voice(slot).carry_on({note_to_freq(noteNumber), velocity, durSamples, duration, int(onsetId), nullptr},
                             fromFreq, glideSec_);
}

} // namespace mforce
