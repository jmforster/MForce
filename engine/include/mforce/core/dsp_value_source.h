#pragma once
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <span>
#include <string_view>
#include <vector>
#include "mforce/core/component.h"
#include "mforce/core/render_context.h"

namespace mforce {

// ---------------------------------------------------------------------------
// ValueSource: the signal contract. A node declares its pins as members
// (pins.h) and writes compute(); the base pulls every pin in declaration
// order, then computes — once per tick of the host's clock.
// ---------------------------------------------------------------------------

struct ValueSource : Component {
  // The two host calls (Part 2 §4-5). Neither is virtual: the base owns the
  // sweep, a node writes the hooks, the same shape as next()/compute().
  //
  // prepare(ctx): once per build, from the host, never per note and never on
  // the audio thread; again only when the host changes the rate or has
  // rebuilt the graph. The only way a node learns the sample rate and the
  // clock. May allocate. Calling it twice with the same context changes
  // nothing a render can see. Forwards to every Auto pin's source, then
  // on_prepare; a Manual pin is prepared by the node in on_prepare.
  void prepare(const RenderContext& ctx) {
    ctx_ = ctx;
    computing_ = false;
    refresh_auto_sources();
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
    computing_ = false;
    refresh_auto_sources();
    for (auto* pin : pins())
      if (pin->pull == Pull::Auto) pin->start(frames);
    on_start(frames);
  }

  virtual void on_prepare(const RenderContext&) {}
  virtual void on_start(int) {}

  // This tick's value. Computed once per tick of the host's clock — every
  // wired Auto pin pulled in declaration order, then compute() — and served
  // to every later pull in the same tick, whoever asks and in whatever
  // order; a node pulled by several consumers advances once. With no host
  // clock every pull computes. A node re-entered during its own compute (a
  // cycle with no tap, which the builders refuse) gets the previous value,
  // and the cycle is reported once.
  float next() {
    if (computing_) { report_cycle_once_(); return cur_; }
    if (ctx_.clock) {
      const Tick t = ctx_.clock->now;
      if (t == lastTick_) return cur_;
      lastTick_ = t;
    }
    computing_ = true;
    for (auto* s : auto_sources()) s->next();
    cur_ = compute();
    computing_ = false;
    return cur_;
  }
  // One sample now, whatever the tick, through every Auto pin: for a node
  // that drives a source at its own rate (a table fill draws N samples at a
  // note's start). The memo holds the last stepped value.
  float step() {
    if (ctx_.clock) lastTick_ = ctx_.clock->now;
    for (auto* s : auto_sources()) s->step();
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
    refresh_auto_sources();   // the pins' sources were just assigned
    return *this;
  }

  float cur_{0.0f};

private:
  void report_cycle_once_() {
    if (cycleReported_) return;
    cycleReported_ = true;
    std::fprintf(stderr, "[graph] cycle without a tap through %s: serving the previous value\n",
                 type_name());
  }

  RenderContext ctx_{};
  Tick lastTick_{-1};
  bool computing_{false};
  bool cycleReported_{false};
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
// constructs with Pull::Manual and reads input.next() there.
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
  if (!pin->source) pin->set(std::make_shared<ConstantSource>(pin->constant));
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

// The tap edge (feedback_loop_design.md): a guarded read of another node's
// memo, never a pull. The source is advanced by its own consumers (or by the
// voice's advance list when only taps reach it), so a tap can close a
// feedback cycle that a pull could not. The z-1 a tap provides is positional:
// a tap evaluated before its source in a tick reads the source's previous
// value. Reads are armored, non-finite scrubbed to 0 and clamped to +-8, so a
// runaway loop saturates audibly instead of poisoning the graph.
struct TapSource final : ValueSource {
  std::shared_ptr<ValueSource> source;

  explicit TapSource(std::shared_ptr<ValueSource> src = nullptr)
    : source(std::move(src)) {}

  // No pins and no hooks: prepare and start reach nothing here. The source
  // is prepared and started by the consumers that pull it.
  float compute() override { return read_(); }
  // The same read, live: a Setup-time read between ticks sees the source's
  // present value, not the memo of the tick before.
  float current() const override { return read_(); }

  const char* type_name() const override { return "Tap"; }

private:
  float read_() const {
    const float v = source ? source->current() : 0.0f;
    if (!std::isfinite(v)) return 0.0f;
    return v < -8.0f ? -8.0f : (v > 8.0f ? 8.0f : v);
  }
};

// The pin bodies that need ValueSource complete. prepare(), start(), next()
// and step() act on the source if there is one; the base calls them for
// Auto pins, the node calls them for its Manual pins.
inline float Param::current() const { return source ? source->current() : constant; }
inline float Param::next() { return source ? source->next() : constant; }
inline float Param::step() { return source ? source->step() : constant; }
inline void Param::prepare(const RenderContext& ctx) { if (source) source->prepare(ctx); }
inline void Param::start(int frames) { if (source) source->start(frames); }
inline float Input::current() const { return source ? source->current() : 0.0f; }
inline float Input::next() { return source ? source->next() : 0.0f; }
inline float Input::step() { return source ? source->step() : 0.0f; }
inline void Input::prepare(const RenderContext& ctx) { if (source) source->prepare(ctx); }
inline void Input::start(int frames) { if (source) source->start(frames); }

} // namespace mforce
