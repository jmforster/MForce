#pragma once
#include <cmath>
#include <memory>
#include <span>
#include <string_view>
#include <vector>
#include "mforce/core/component.h"
#include "mforce/core/render_context.h"

namespace mforce {

// ---------------------------------------------------------------------------
// Temporary instrumentation for the per-tick-memo migration; it goes with
// the memo. The render driver bumps `sample` once per pulled sample; a
// consumer advances a pin through ValueSource::advance(), which stamps the
// source with the sample; RefSource::read() compares the stamp with the
// current sample to tell a previous-sample read from a same-sample read.
// Off unless mforce_cli sees MFORCE_TRACE_SHARING; plain statics because
// the engine renders on one thread.
// ---------------------------------------------------------------------------

struct SharingTrace {
  static inline long long sample = 0;
  static inline long long lagReads = 0;       // wrapped consumer read a previous-sample value
  static inline long long sameTickTaps = 0;   // guarded (tap) read saw this sample's value
  static inline bool enabled = false;
};

// ---------------------------------------------------------------------------
// ValueSource: the signal contract. A node declares its pins as members
// (pins.h) and writes compute(); the base advances every pin in declaration
// order, then computes.
// ---------------------------------------------------------------------------

struct ValueSource : Component {
  // Consumers advance a pin through advance(), never next() directly, so
  // the SharingTrace knows at which sample this node last advanced. The
  // stamp is set after next() returns: a read that happens during this
  // node's own pull (a tap closing a loop) sees the previous sample.
  float advance() {
    const float v = next();
    if (SharingTrace::enabled) lastAdvancedSample_ = SharingTrace::sample;
    return v;
  }
  long long lastAdvancedSample_{-1};

  // Forwards to every pin, then to the node's own reset.
  virtual void prepare(const RenderContext& ctx, int frames) {
    for (auto* pin : pins()) pin->prepare(ctx, frames);
    on_prepare(ctx, frames);
  }
  virtual void on_prepare(const RenderContext&, int) {}

  // Advance every wired Auto pin in declaration order, then compute. A
  // node's work is compute(). Virtual, with a default compute(), only
  // while the node families migrate: the close of checkpoint 2.1 seals
  // next() and makes compute() pure, so the compiler proves no override
  // remains.
  virtual float next() {
    for (auto* s : pulled())
      if (*s) (*s)->advance();
    cur_ = compute();
    return cur_;
  }
  virtual float compute() { return cur_; }
  virtual float current() const { return cur_; }

  // False when this node reads its `frequency` pin ONCE at note start and
  // never again (KSPianoString: init_note), so an articulated/bent frequency
  // is silently inert on it. The loader uses this to warn by name at load.
  // Default true: ordinary pulled-per-sample consumption tracks a moving
  // frequency.
  virtual bool tracks_frequency_live() const { return true; }

  // Phase delay, in samples, that this node's processing imposes on a signal
  // at `hz` passing THROUGH it, from its current parameter state. DelayLine
  // sums this over its loop members to keep a tap-closed feedback loop on
  // pitch while in-loop filters move (feedback_loop_design.md §5).
  // Memoryless nodes (shapers, gains, combiners) and generators keep the
  // default 0; only stateful in-loop processors override. Negative = phase
  // lead (highpass/bandpass below cutoff). Non-const so overrides may cache
  // the atan2 work behind a same-inputs guard.
  virtual float phase_delay_at(float /*hz*/) { return 0.0f; }

  // Per-note determinism: re-anchor this node's stochastic draw streams to
  // their seeds. Called at an IN-LINE note's Setup only (never after a
  // fresh prepare, which already seeds — and consumes layout draws, so a
  // post-prepare call would NOT be byte-neutral). Default no-op;
  // RNG-bearing sources override.
  virtual void reseed() {}

protected:
  float cur_{0.0f};
};

// A node that produces a signal from time and its parameters alone.
struct Generator : ValueSource {
  // The unit range of the output, for the UI and the queued universal
  // parameters: None when the output is not a signal in [-1,1] or [0,1].
  enum class Range { Bipolar, Unipolar, None };
  virtual Range range() const { return Range::None; }
};

// A node that processes a signal: it owns the primary input. Further inputs
// are declared by the subclass. A node that must pull its input itself (only
// between gaps, once per clone, at the moment the old code read it)
// constructs with Pull::Manual and reads input.advance() in compute().
struct Processor : ValueSource {
  Input input;
  explicit Processor(Pull inputPull = Pull::Auto)
    : input{this, "source", Input::RequiredInput, nullptr, false, inputPull} {}
  SourceCategory category() const override { return SourceCategory::Filter; }
};

struct ConstantSource final : Generator {
  explicit ConstantSource(float v) : v_(v) { cur_ = v; }
  void set(float v) { v_ = v; }
  float compute() override { return v_; }
  const char* type_name() const override { return "Constant"; }
private:
  float v_{0.0f};
};

// Transparent wrapper for shared sources with multiple consumers.
// The primary consumer calls next() on the real source; secondary consumers
// use a RefSource which just reads current() without advancing.
// Created automatically by the UI when multiple inputs wire to the same output.
// guard=true is the TAP form (feedback_loop_design.md): reads are armored —
// non-finite scrubbed to 0, clamped to +-8 — so a runaway loop saturates
// audibly instead of poisoning the graph. The z-1 a tap provides is
// positional (tap consumers evaluate before their source each tick), not
// implemented here.
struct RefSource final : ValueSource {
  std::shared_ptr<ValueSource> source;
  bool guard{false};

  explicit RefSource(std::shared_ptr<ValueSource> src, bool g = false)
    : source(std::move(src)), guard(g) {}

  void prepare(const RenderContext& /*ctx*/, int /*frames*/) override {} // primary consumer prepares the real source
  float compute() override { return read(); }
  float current() const override { return read(); }

  const char* type_name() const override { return "RefSource"; }

private:
  float read() const {
    float v = source ? source->current() : 0.0f;
    if (SharingTrace::enabled && source) {
      const bool advancedThisSample = source->lastAdvancedSample_ == SharingTrace::sample;
      if (guard && advancedThisSample) ++SharingTrace::sameTickTaps;
      if (!guard && !advancedThisSample) ++SharingTrace::lagReads;
    }
    if (!guard) return v;
    if (!std::isfinite(v)) return 0.0f;
    return v < -8.0f ? -8.0f : (v > 8.0f ? 8.0f : v);
  }
};

// The pin bodies that need ValueSource complete. A Manual pin is driven by
// its node: the base neither prepares nor advances it, and the node pulls
// it through advance() where and when it needs the value.
inline float Param::current() const { return source ? source->current() : constant; }
inline float Param::advance() { return source ? source->advance() : constant; }
inline void Param::prepare(const RenderContext& ctx, int frames) {
  if (source && pull == Pull::Auto) source->prepare(ctx, frames);
}
inline float Input::current() const { return source ? source->current() : 0.0f; }
inline float Input::advance() { return source ? source->advance() : 0.0f; }
inline void Input::prepare(const RenderContext& ctx, int frames) {
  if (source && pull == Pull::Auto) source->prepare(ctx, frames);
}

} // namespace mforce
