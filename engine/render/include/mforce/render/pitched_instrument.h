#pragma once
#include "mforce/render/instrument.h"
#include "mforce/render/node_capture.h"
#include "mforce/render/perform_source.h"
#include "mforce/render/ring_out.h"
#include "mforce/render/voice_pool.h"
#include <memory>
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
  // a held line (0 retunes at once); `pool` the voices, built for `state`,
  // the instrument as played.
  PitchedInstrument(InstrumentClass cls, float glideSec, VoicePool pool,
                    std::shared_ptr<InstrumentState> state);

  const InstrumentClass& instrument_class() const override { return class_; }
  // Each note of the part in the order given, then an open line is ended.
  // A pitched instrument plays no hits.
  void play(const Performance& part) override;
  // The instrument's facts, then every voice's.
  void prepare(const RenderContext& c) override;
  // Every voice's clones of one file node report as that node, once, in id
  // order.
  void collect_faults(std::vector<FaultReport>& out) override;

  const VoicePool& pool() const { return pool_; }
  // The instrument as played (mod wheel, channel pressure), outliving every
  // note; each voice's PerformSource smooths and reports it.
  const std::shared_ptr<InstrumentState>& instrument_state() const { return state_; }
  float glide_seconds() const { return glideSec_; }
  NodeCapture& capture() { return capture_; }
  const NodeCapture& capture() const { return capture_; }

private:
  // The live player is the instrument's other driver: it starts and pulls
  // the pool's voices itself.
  friend class LivePlayer;

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
  std::shared_ptr<InstrumentState> state_;
  VoicePool pool_;
  NodeCapture capture_;
  HeldLine line_;
  bool warnedNoEnvelopes_{false};
};

} // namespace mforce
