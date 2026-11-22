#pragma once
#include "mforce/contract/pitch_curve.h"
#include "mforce/render/instrument.h"
#include "mforce/render/node_capture.h"
#include "mforce/render/perform_source.h"
#include "mforce/render/ring_out.h"
#include "mforce/render/voice_pool.h"
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace mforce {

// A pitched instrument: plays a part's notes on its pool of voices, each a
// clone of the patch's graph. A fresh note takes the next voice in turn and
// renders with its release inside its own length; notes marked `hold` on a
// voice that can hold form one monophonic held line, rendered on one voice
// in pieces and released when a note without `hold` ends it.
class PitchedInstrument final : public Instrument {
public:
  // `cls` from the instrument block; `glideSec` is the legato retune time of
  // a held line (0 retunes at once).
  PitchedInstrument(InstrumentClass cls, float glideSec);

  const InstrumentClass& instrument_class() const override { return class_; }
  // Each note of the part in the order given, then an open line is ended.
  // A pitched instrument plays no hits.
  void play(const Performance& part) override;
  // The instrument's facts, then every voice's.
  void prepare(const RenderContext& c) override;
  // Every voice's clones of one file node report as that node, once, in id
  // order.
  void collect_faults(std::vector<FaultReport>& out) override;

  VoicePool& pool() { return pool_; }
  const VoicePool& pool() const { return pool_; }
  // The instrument as played (mod wheel, channel pressure), outliving every
  // note; each voice's PerformSource smooths and reports it.
  const std::shared_ptr<InstrumentState>& instrument_state() const { return state_; }
  float glide_seconds() const { return glideSec_; }
  NodeCapture& capture() { return capture_; }
  const NodeCapture& capture() const { return capture_; }

  // --- The live calls: a host pulls a prepared voice itself ---------------
  // A voice prepared for a streaming caller, which ticks it, pulls it and its
  // tap-only tails once per sample for `durSamples` samples.
  struct StreamingVoice {
    std::shared_ptr<ValueSource> source;
    int   durSamples{0};
    // The note's velocity times the instrument's volume: a streaming caller
    // never passes through render().
    float gain{1.0f};
    std::shared_ptr<PerformSource> performSource;
    // The voice's tap-only tails, pulled once per sample after the source.
    std::span<const std::shared_ptr<ValueSource>> advanceList;
    std::shared_ptr<SampleClock> clock;
    // A sample begins: once per sample, before source->next().
    void tick() { clock->tick(); if (performSource) performSource->tick(); }
  };
  // The note on the next voice in the rotation.
  StreamingVoice prepare_voice(float noteNumber, float velocity, float duration,
                               const PitchCurve* curve = nullptr);
  // The note on voice `slot`, the rotation untouched. No trigger fires.
  StreamingVoice prepare_voice_at(int slot, float noteNumber, float velocity, float duration,
                                  const PitchCurve* curve = nullptr);
  // An idle slot, rotating from the scheduled rotation; -1 when every slot
  // sounds. The caller owns the steal policy and the threading.
  int acquire_voice();
  void release_voice(int slot);
  void release_all_voices();
  // A held line's next note delivered into a voice the caller is pulling.
  void continue_voice_live(int slot, float noteNumber, float velocity, float duration,
                           float fromFreq, float onsetId);

private:
  // The held line: one per instrument. Its voice renders each note's samples
  // and stays warm between them; the release, the tail and the ring-out run
  // when a note without `hold` ends it.
  struct HeldLine {
    bool  open{false};
    int   voice{-1};
    float startTime{0.0f};      // timeline seconds of the line's start
    std::vector<float> buf;     // samples rendered since the line's start
    float gain{1.0f};           // the first note's velocity, for the whole line
    float lastFreq{0.0f};
    float lastNote{0.0f};       // the containment report's id
    int   lastSamples{0};       // the releasing note's length
  };

  void play_note(const PerformedNote& pn);
  void continue_line(const PerformedNote& pn, const Voice::Note& note);
  void open_line(int v, const PerformedNote& pn, const Voice::Note& note);
  void play_classic(int v, const PerformedNote& pn, const Voice::Note& note);
  void render_into(int v, std::vector<float>& buf, int startFrame, int n, float gain);
  void ring_out(int v, std::vector<float>& buf, int startFrame, float gain, RingOut& ring);
  void finish_line();
  int frame_of(float seconds) const { return int(seconds * float(render_context().sampleRate)); }

  InstrumentClass class_;
  float glideSec_;
  std::shared_ptr<InstrumentState> state_{std::make_shared<InstrumentState>()};
  VoicePool pool_;
  NodeCapture capture_;
  HeldLine line_;
  bool warnedNoEnvelopes_{false};
  std::vector<uint8_t> slotInUse_;   // the live calls' slot table
};

} // namespace mforce
