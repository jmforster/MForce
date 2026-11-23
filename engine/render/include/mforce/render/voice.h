#pragma once
#include "mforce/contract/pitch_curve.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/patch/built_graph.h"
#include "mforce/patch/graph.h"
#include "mforce/patch/instrument_block.h"
#include "mforce/patch/note_signals.h"
#include "mforce/render/perform_source.h"
#include "mforce/source/envelope/envelope.h"
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace mforce {

// One clone of an instrument's graph, playable a note at a time. It owns the
// graph's nodes, its PerformSource and the adapters every PerformNode of the
// graph resolves to, its sample clock, and the lists a note's Setup walks.
class Voice {
public:
  // A note as the voice takes it: base frequency, velocity, length in samples
  // and in seconds, onset id, and the pitch curve it carries (not owned).
  struct Note {
    float freq;
    float velocity;
    int   samples;
    float seconds;
    int   onset;
    const PitchCurve* curve;
  };

  // A signal node of the voice and its file id.
  struct Signal { std::string id; ValueSource* node; };

  using NodeMap = std::unordered_map<std::string, std::shared_ptr<Component>>;

  // What the build made, by node id: the output; the tap-only tails; the
  // envelopes a held line gates and those on the trigger list; the dynamic
  // pins (the node, its setting, the node that feeds it). Each list is in the
  // graph's node order.
  struct Description {
    struct Pin { std::string node, setting, source; };
    std::string output;
    std::vector<std::string> tails, heldEnvelopes, triggerEnvelopes;
    std::vector<Pin> dynamicPins;
  };

  // The voice `graph` describes, built for the instrument `block` and the
  // instrument's played state. A non-null `warnings` receives what the build
  // had to say.
  Voice(const Graph& graph, const InstrumentBlock& block, std::shared_ptr<InstrumentState> state,
        std::vector<std::string>* warnings);

  // The host's facts, completed with this voice's clock: every node of the
  // voice is prepared with them and computes once per tick of the clock.
  void prepare(const RenderContext& ctx);

  // A note's Setup: the note into the PerformSource (with its compiled bend
  // when it has a curve), the dynamic pins pushed, the envelopes gated when
  // `gated`, the graph and its tap-only tails started, the triggers fired.
  void start(const Note& n, bool gated);

  // The next note of a held line: every signal's random draws re-anchored,
  // the glide from `fromFreq`, the note into the PerformSource, the triggers
  // fired. The graph is not started again and the dynamic pins keep their
  // line-start values. Allocates nothing.
  void carry_on(const Note& n, float fromFreq, float glideSec);

  // The gate closes: every envelope's release is laid out against the
  // releasing note's length. Returns the samples of release still to play.
  int release(int lastNoteSamples);
  // The envelopes ungated, as a voice that played no held line leaves them.
  void ungate();

  // One sample: the clock and the perform clock, the root pull, then the
  // tap-only tails, which their tap consumers read a sample late.
  float next() {
    clock_->tick();
    if (performSource_) performSource_->tick();
    const float s = source_->next();
    for (auto& a : tails_) a->next();
    return s;
  }

  // Whether a held line can gate this voice: it has envelopes outside every
  // Multiplex template.
  bool can_hold() const { return !envelopes_.empty(); }

  // Every node of the voice by file id, for a host that reaches one.
  const NodeMap& nodes() const { return nodes_; }
  // The signal nodes, in id order: faults and capture walk them.
  const std::vector<Signal>& signals() const { return signals_; }
  const Description& description() const { return description_; }
  // Where each pin's constant lives in this voice: the build's list, less the
  // constants the voice itself read at construction (each Envelope's
  // `trigger`, which decides the trigger list). Every voice built from one
  // graph has the same list, in the same order.
  std::span<const BuiltGraph::Constant> constants() const { return constants_; }

private:
  // A dynamic pin: a setting the patch drives once per note. A setting is a
  // number its node reads as the note starts; it has no source to pull, so
  // the value is pushed through set_setting. `chain` is the signal wired to
  // the pin, evaluated once at the note's Setup.
  struct PushBinding {
    std::shared_ptr<Component>   consumer;
    std::string                  paramName;
    std::shared_ptr<ValueSource> chain;
  };

  void deliver(const Note& n);
  void gate();
  void start_graph(int samples);
  void fire_triggers();
  void aim_glide(float fromFreq, float toFreq, float glideSec, int samples);

  std::shared_ptr<ValueSource>   source_;
  std::shared_ptr<PerformSource> performSource_;
  NoteSignals                    note_;
  std::vector<PushBinding>       pushBindings_;
  // Heap-owned so the context's clock pointer survives the voice moving.
  std::shared_ptr<SampleClock>   clock_{std::make_shared<SampleClock>()};
  RenderContext                  ctx_;
  // The tap-only tails: never reached by the root pull, pulled once per
  // sample after it.
  std::vector<std::shared_ptr<ValueSource>> tails_;
  NodeMap                        nodes_;
  std::vector<Signal>            signals_;
  // Raw pointers into nodes the graph owns. The Envelopes whose `trigger` is
  // read at each note's Setup, and the Envelopes a held line gates: every
  // Envelope of the graph but those only a Multiplex template holds.
  std::vector<Envelope*>         triggers_;
  std::vector<Envelope*>         envelopes_;
  // The voice's one glide, laid out again in place for each carried note, and
  // the compiled bend of its current note. The voice keeps both so the
  // PerformSource never drops their last reference: a carried note, which
  // may arrive on the audio thread, frees nothing.
  std::shared_ptr<Envelope>      glide_;
  std::shared_ptr<Envelope>      bend_;
  Description                    description_;
  std::vector<BuiltGraph::Constant> constants_;
};

} // namespace mforce
