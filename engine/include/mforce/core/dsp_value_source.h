#pragma once
#include <cassert>
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

  // The two host calls (Part 2 §4-5). Neither is virtual: the base owns the
  // sweep, a node writes the hooks, the same shape as next()/compute().
  //
  // prepare(ctx): once per build, from the host, never per note and never on
  // the audio thread; again only when the host changes the rate or has
  // rebuilt the graph. The only way a node learns the sample rate. May
  // allocate. Calling it twice with the same context changes nothing a
  // render can see. Forwards to every Auto pin's source, then on_prepare;
  // a Manual pin is prepared by the node in on_prepare.
  void prepare(const RenderContext& ctx) {
    ctx_ = ctx;
    for (auto* pin : pins())
      if (pin->pull == Pull::Auto) pin->prepare(ctx);
    on_prepare(ctx);
  }
  // start(frames): a span of `frames` samples begins now — reset per-note
  // state and lay out anything that depends on the length. May not
  // allocate. Forwards to every Auto pin's source, then on_start; a Manual
  // pin is started by the node in on_start.
  void start(int frames) {
    assert(prepared() && "start() before prepare(ctx)");
    for (auto* pin : pins())
      if (pin->pull == Pull::Auto) pin->start(frames);
    on_start(frames);
  }

  virtual void on_prepare(const RenderContext&) {}
  virtual void on_start(int) {}

  // Advance every wired Auto pin in declaration order, then compute. A
  // node's work is compute(); next() is the same for every node.
  float next() {
    for (auto* s : auto_sources())
      if (*s) (*s)->advance();
    cur_ = compute();
    return cur_;
  }
  virtual float compute() = 0;
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
  // A node reads its rate here and keeps no copy of its own.
  int sample_rate() const { return ctx_.sampleRate; }

  // The host's facts as prepare(ctx) left them, for the node's own use: a
  // setter that rebuilds rate-derived state asks prepared() first, and a
  // part a node makes after its prepare is prepared with render_context().
  // Written only by prepare. Nobody outside reads them: the rate reaches a
  // node only through prepare(ctx), and a host that makes a node late hands
  // it the context it prepared the owner with (ensure_constant below).
  const RenderContext& render_context() const { return ctx_; }
  bool prepared() const { return ctx_.sampleRate > 0; }

  // A node assigning itself (Envelope::replace_stages) assigns its own
  // state; the context the host gave this object stays, like the registry.
  ValueSource& operator=(const ValueSource& o) {
    Component::operator=(o);
    cur_ = o.cur_;
    return *this;
  }

  float cur_{0.0f};

private:
  RenderContext ctx_{};
};

// A node that produces a signal from time and its parameters alone.
struct Generator : ValueSource {
  // The unit range of the output, for the UI and the queued universal
  // parameters: None when the output is not a signal in [-1,1] or [0,1].
  enum class Range { Bipolar, Unipolar, None };
  virtual Range range() const { return Range::None; }
};

// A node that processes a signal: it owns the primary input. Further inputs
// are declared by the subclass. A node that pulls its input itself (only
// between gaps, once per clone, or at a fixed point of its own compute())
// constructs with Pull::Manual and reads input.advance() there.
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

// A per-note push needs a constant to write into. Returns the Param's source
// as a ConstantSource, first giving an unwired pin one at the pin's constant;
// nullptr when the node has no such Param or the pin is wired to another
// source. This form is for a node nobody has prepared yet (the loader,
// building a graph): the constant learns the rate when the owner's prepare
// forwards to its pins.
inline std::shared_ptr<ConstantSource> ensure_constant(ValueSource& node, std::string_view name) {
  Param* pin = node.param(name);
  if (!pin) return nullptr;
  if (!pin->source) pin->source = std::make_shared<ConstantSource>(pin->constant);
  return std::dynamic_pointer_cast<ConstantSource>(pin->source);
}

// The same, for a node the host has already prepared (a Multiplex pushing a
// clone's setting at note Setup): the caller hands the context it prepared
// the owner with, and a constant made now is ready before the next start().
// The caller knows the context; the node is not asked for it.
inline std::shared_ptr<ConstantSource> ensure_constant(ValueSource& node, std::string_view name,
                                                       const RenderContext& ctx) {
  Param* pin = node.param(name);
  const bool madeNow = pin && !pin->source;
  auto cs = ensure_constant(node, name);
  if (cs && madeNow) cs->prepare(ctx);
  return cs;
}

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

  // No pins and no hooks: prepare and start reach nothing here. The
  // primary consumer prepares and starts the real source.
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

// The pin bodies that need ValueSource complete. prepare(), start() and
// advance() act on the source if there is one; the base calls them for
// Auto pins, the node calls them for its Manual pins.
inline float Param::current() const { return source ? source->current() : constant; }
inline float Param::advance() { return source ? source->advance() : constant; }
inline void Param::prepare(const RenderContext& ctx) { if (source) source->prepare(ctx); }
inline void Param::start(int frames) { if (source) source->start(frames); }
inline float Input::current() const { return source ? source->current() : 0.0f; }
inline float Input::advance() { return source ? source->advance() : 0.0f; }
inline void Input::prepare(const RenderContext& ctx) { if (source) source->prepare(ctx); }
inline void Input::start(int frames) { if (source) source->start(frames); }

} // namespace mforce
