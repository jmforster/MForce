#pragma once
#include <atomic>
#include <cassert>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <span>
#include <string_view>
#include <vector>
#include "mforce/core/component.h"
#include "mforce/core/render_context.h"

namespace mforce {

// What a node can report from a render path instead of throwing or
// printing. One bit each; a node accumulates them until the host reads and
// clears them after a render (Instrument::collect_faults).
enum class Fault : unsigned char {
  None             = 0,
  FrequencyClamped = 1,    // a non-positive frequency was clamped to the pin's minimum
  TableClamped     = 2,    // a table fill's length was clamped to one sample
  NeverPrepared    = 4,    // start() or next() on a node no host prepared
  Clockless        = 8,    // next() with a rate but no host clock: every pull computes
  CycleWithoutTap  = 16,   // re-entered through a cycle with no tap; served the previous value
};
constexpr unsigned char fault_bit(Fault f) { return static_cast<unsigned char>(f); }
inline const char* fault_name(Fault f) {
  switch (f) {
    case Fault::FrequencyClamped: return "frequency clamped";
    case Fault::TableClamped:     return "table length clamped";
    case Fault::NeverPrepared:    return "never prepared";
    case Fault::Clockless:        return "pulled without a host clock";
    case Fault::CycleWithoutTap:  return "cycle without a tap";
    case Fault::None:             break;
  }
  return "none";
}

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
  // on_prepare; a Manual pin is prepared by the node in on_prepare. A zero
  // rate is a host error and throws here, where throwing is allowed; a node
  // never sees one. Re-entered through a cycle with no tap (hand-wired
  // before the host calls), it returns with the fault set instead of
  // overflowing the stack.
  void prepare(const RenderContext& ctx) {
    if (ctx.sampleRate <= 0) throw std::invalid_argument("prepare: the context has no sample rate");
    if (computing_) { fault(Fault::CycleWithoutTap); return; }
    computing_ = true;
    ctx_ = ctx;
    lastTick_ = -1;           // a tick means something only against this clock
    refresh_auto_sources();   // the faults stay: a host reads them, a second prepare does not
    for (auto* pin : pins())
      if (pin->pull == Pull::Auto) pin->prepare(ctx);
    on_prepare(ctx);
    computing_ = false;
  }
  // start(frames): a span of `frames` samples begins now — reset per-note
  // state and lay out anything that depends on the length. May not
  // allocate. Forwards to every Auto pin's source, then on_start; a Manual
  // pin is started by the node in on_start.
  // On a node no host prepared it sets the fault and does nothing else:
  // the Debug assert stays the Debug contract, the Release build reports
  // instead of dividing by zero in an on_start. (next() and step() on such
  // a node compute as a bare node always has, and report the same fault.)
  void start(int frames) {
    assert(prepared() && "start() before prepare(ctx)");
    if (!prepared()) { fault(Fault::NeverPrepared); return; }
    if (computing_) { fault(Fault::CycleWithoutTap); return; }
    computing_ = true;
    refresh_auto_sources();
    for (auto* pin : pins())
      if (pin->pull == Pull::Auto) pin->start(frames);
    on_start(frames);
    computing_ = false;
  }

  virtual void on_prepare(const RenderContext&) {}
  virtual void on_start(int) {}

  // This tick's value. Computed once per tick of the host's clock — every
  // wired Auto pin pulled in declaration order, then compute() — and served
  // to every later pull in the same tick, whoever asks and in whatever
  // order; a node pulled by several consumers advances once. The value it
  // replaces is kept for the tap edge (previous()). With no host clock
  // every pull computes, and a prepared node says so with a fault the host
  // reads. A node re-entered during its own compute (a cycle with no tap,
  // which the builders refuse) gets the previous value and a fault.
  float next() {
    if (computing_) { fault(Fault::CycleWithoutTap); return cur_; }
    if (ctx_.clock) {
      const Tick t = ctx_.clock->now;
      if (t == lastTick_) return cur_;
      lastTick_ = t;
    } else if (!prepared()) {  // the cold branches: no host clock
      fault(Fault::NeverPrepared);   // no rate: a bare node computes as it always has, and says so
    } else {
      fault(Fault::Clockless);       // a rate but no clock: a bare node in a test, or a host that forgot its clock
    }
    prev_ = cur_;             // the tick is claimed: what stood here is now the previous tick's
    computing_ = true;
    for (auto* s : auto_sources()) s->next();
    cur_ = compute();
    computing_ = false;
    return cur_;
  }
  // One sample now, whatever the tick, through every Auto pin: for a node
  // that drives a source at its own rate (a table fill draws N samples at a
  // note's start). The memo holds the last stepped value. Two limits, by
  // construction: a Manual pin or an owned sub-source inside the stepped
  // subgraph is pulled by its node with next() and so serves the memo for
  // the rest of the fill; and a node reached on two pins of the subgraph
  // steps once per pin. The two callers (the table fills) drive a private
  // excitation source; a shared or Manual-pulling excitation is not
  // supported under step().
  float step() {
    if (!prepared()) fault(Fault::NeverPrepared);
    if (ctx_.clock) lastTick_ = ctx_.clock->now;
    prev_ = cur_;
    for (auto* s : auto_sources()) s->step();
    cur_ = compute();
    return cur_;
  }
  virtual float compute() = 0;
  virtual float current() const { return cur_; }

  // The value this node held at the end of the previous tick: cur_ until the
  // node claims this tick, the value cur_ replaced from then on — the same
  // number whether the asker comes before, during or after this node's
  // compute. What the tap edge reads (feedback_loop_design.md): a tap is one
  // sample late wherever it sits. With no host clock there is no "this
  // tick" and the read is cur_, as every pull computes. Not virtual: a
  // stateless view's live current() has no previous value but the one it
  // computed last tick.
  float previous() const {
    return claimed_tick() ? prev_ : cur_;
  }

  // The faults this node has reported since the host last cleared them.
  // Read and cleared by the host after a render (Instrument::collect_faults),
  // never on the audio thread.
  unsigned char faults() const { return faults_.load(std::memory_order_relaxed); }
  void clear_faults() { faults_.store(0, std::memory_order_relaxed); }

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

  // True once this node has claimed the host's current tick (it is
  // computing, or has computed, this tick); false with no host clock. For a
  // node whose live read must say something different between ticks and
  // within one (TapSource).
  bool claimed_tick() const { return ctx_.clock && lastTick_ == ctx_.clock->now; }

  // Report from a render path. A relaxed OR on a lock-free byte: one writer
  // per node on the audio thread, one reader after the render; no lock, and
  // nothing on the common path.
  void fault(Fault f) { faults_.fetch_or(fault_bit(f), std::memory_order_relaxed); }

  // The host's facts as prepare(ctx) left them, for the node's own use: a
  // setter that rebuilds rate-derived state asks prepared() first, and a
  // part a node makes after its prepare is prepared with render_context().
  // Written only by prepare. Nobody outside reads them: the rate reaches a
  // node only through prepare(ctx), and a host that makes a node late hands
  // it the context it prepared the owner with (ensure_constant below).
  const RenderContext& render_context() const { return ctx_; }
  bool prepared() const { return ctx_.sampleRate > 0; }

  // A node assigning itself (Envelope::replace_stages) assigns its own
  // state, both ticks' values among it; the context the host gave this
  // object stays, like the registry.
  // No sweep refresh here: a base's assignment runs before the derived
  // members are assigned, so the pins still hold their old sources at this
  // point. The node that assigns itself re-sets its wires afterwards
  // (Envelope::replace_stages), and the setter refreshes the sweep.
  ValueSource& operator=(const ValueSource& o) {
    Component::operator=(o);
    cur_ = o.cur_;
    prev_ = o.prev_;
    faults_.store(o.faults_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    return *this;
  }

  float cur_{0.0f};

private:
  RenderContext ctx_{};
  Tick lastTick_{-1};
  float prev_{0.0f};        // what cur_ held before this tick's compute
  std::atomic<unsigned char> faults_{0};
  bool computing_{false};
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
// voice's tail list when only taps reach it), so a tap can close a feedback
// cycle that a pull could not. A tap is one sample late by rule: it reads
// the value its source held at the end of the previous tick, wherever the
// tap sits in the pull. Reads are armored, non-finite scrubbed to 0 and
// clamped to +-8, so a runaway loop saturates audibly instead of poisoning
// the graph.
struct TapSource final : ValueSource {
  std::shared_ptr<ValueSource> source;

  explicit TapSource(std::shared_ptr<ValueSource> src = nullptr)
    : source(std::move(src)) {}

  // No pins and no hooks: prepare and start reach nothing here. The source
  // is prepared and started by whatever pulls it — its consumers, or the
  // voice's tail list when only taps reach it.
  float compute() override { return guard_(source ? source->previous() : 0.0f); }
  // The read that delivers: a consumer reads its pins after its sweep
  // through current(), so once the tap has claimed this tick it answers
  // with the memo; before it is pulled it answers live with the source's
  // present value. Between ticks (a Setup-time read) the clock holds at the
  // last rendered tick, so a never-pulled tap answers live — what the coming
  // tick delivers — while a tap pulled in that tick still answers with its
  // memo, one sample older; a step() through such a tap reads the same
  // memo. Part 3's pins own the exact Setup-time read.
  float current() const override {
    return claimed_tick() ? cur_ : guard_(source ? source->current() : 0.0f);
  }

  const char* type_name() const override { return "Tap"; }

private:
  static float guard_(float v) {
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
