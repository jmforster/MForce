#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/fast_math.h"
#include "mforce/source/additive/formant.h"
#include "mforce/core/randomizer.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <memory>
#include <limits>

namespace mforce {

// ---------------------------------------------------------------------------
// ExpandRule — controls generation of "sub-partials" around each primary
// partial. Ported from legacy PartialsExpandRule.cs.
// ---------------------------------------------------------------------------
struct ExpandRule {
  int count{2};          // sub-partials per side
  int recurse{0};        // how many times to re-expand
  float spacing1{0.5f}, spacing2{0.5f};  // semitone spacing (start/end)
  float dt1{0.01f}, dt2{0.01f};          // random detune on sub-partials
  float loPct1{0.1f}, loPct2{0.1f};      // minimum amplitude fraction
  float power1{1.0f}, power2{1.0f};      // amplitude curve power
  float po1{0.0f}, po2{0.0f};            // phase offset for sub-partials
};

// ExpandRuleNode — graph-node wrapper around ExpandRule. Lets patches
// declare an ExpandRule as a top-level node and reference it from a
// Partials host via { "ref": "..." }. Mirrors the role of Formant —
// a parametric building block consumed by a host node, with no audio
// output of its own. The host snapshots fields at load/prepare time
// via to_struct().
struct ExpandRuleNode final : ValueSource {
  // The host reads the pins with to_struct(); nothing pulls them per sample,
  // so all ten are Manual and this node never prepares or advances them.
  Param spacing1{this, "spacing1", 0.5f,  0.0f, 24.0f, "semis",  Pull::Manual};
  Param spacing2{this, "spacing2", 0.5f,  0.0f, 24.0f, "semis",  Pull::Manual};
  Param dt1     {this, "dt1",      0.01f, 0.0f, 1.0f,  "0-1",    Pull::Manual};
  Param dt2     {this, "dt2",      0.01f, 0.0f, 1.0f,  "0-1",    Pull::Manual};
  Param loPct1  {this, "loPct1",   0.1f,  0.0f, 1.0f,  "0-1",    Pull::Manual};
  Param loPct2  {this, "loPct2",   0.1f,  0.0f, 1.0f,  "0-1",    Pull::Manual};
  Param power1  {this, "power1",   1.0f,  0.0f, 10.0f, "",       Pull::Manual};
  Param power2  {this, "power2",   1.0f,  0.0f, 10.0f, "",       Pull::Manual};
  Param po1     {this, "po1",      0.0f,  0.0f, 1.0f,  "cycles", Pull::Manual};
  Param po2     {this, "po2",      0.0f,  0.0f, 1.0f,  "cycles", Pull::Manual};
  Setting<int> count  {this, "count",   2, 0, 16};
  Setting<int> recurse{this, "recurse", 0, 0, 4};

  // Snapshot to a plain ExpandRule for consumption by Partials. Reads the
  // current() of each field — at load time these are ConstantSource values,
  // at prepare time they could be the current value of a wired envelope.
  ExpandRule to_struct() const {
    ExpandRule r;
    r.count = count.value;
    r.recurse = recurse.value;
    r.spacing1 = spacing1.current();
    r.spacing2 = spacing2.current();
    r.dt1 = dt1.current();
    r.dt2 = dt2.current();
    r.loPct1 = loPct1.current();
    r.loPct2 = loPct2.current();
    r.power1 = power1.current();
    r.power2 = power2.current();
    r.po1 = po1.current();
    r.po2 = po2.current();
    return r;
  }

  const char* type_name() const override { return "ExpandRule"; }
  SourceCategory category() const override { return SourceCategory::Additive; }

  float next() override { return 0.0f; }
  float current() const override { return 0.0f; }
};

// ---------------------------------------------------------------------------
// IPartials — interface for partial rendering engines.
// Uses partials_prepare / partials_next to avoid collision with
// ValueSource::prepare / ValueSource::next.
// ---------------------------------------------------------------------------
struct IPartials {
  virtual ~IPartials() = default;
  virtual void partials_prepare(const RenderContext& ctx, int frames) = 0;
  virtual void partials_next() = 0;
  virtual int partial_count() const = 0;
  virtual float get_partial_value(float amplitude, float frequency, float phaseDiff,
                                  int index, IFormant* formant, float formantWeight,
                                  float formantFloor) = 0;

  // Batched entry point: add every partial's contribution for one sample to
  // `acc` and return it. One virtual call per sample instead of one per
  // partial — that is what lets the per-partial body inline and hoist its
  // per-sample invariants.
  //
  // The accumulator is threaded in/out rather than each implementer returning
  // a subtotal, so a nested set (MultiPartials) sums in exactly the same order
  // as one flat loop over all partials. Float addition is not associative:
  // subtotalling would change the last bits of the output.
  //
  // The default body is the exact loop FullAdditiveSource used to run
  // (NaN = past cutoff, gate that partial only), so an implementer that
  // doesn't override keeps identical behaviour.
  virtual float sum_partials(float acc, float amplitude, float frequency,
                             float phaseDiff, IFormant* formant,
                             float formantWeight, float formantFloor) {
    const int n = partial_count();
    for (int i = 0; i < n; ++i) {
      float v = get_partial_value(amplitude, frequency, phaseDiff, i,
                                  formant, formantWeight, formantFloor);
      if (std::isnan(v)) continue;
      acc += v;
    }
    return acc;
  }
};

// Rng stream layer ids (see stream_seed in core/randomizer.h). Every random
// consumer in Partials draws from its own seeded stream — per (note, partial)
// for the layers with per-partial state, per note for the single-stream
// layers — so no draw depends on how many draws any other consumer made.
// Previously one shared rng_ served all layers in (sample, partial) draw
// order; a partial flickering across the 16 kHz cutoff (early return before
// its bandwidth draw) then shifted every downstream draw and re-rolled all
// noise in the render (measured ±3 dB note-contour changes).
enum PartialsRngLayer : uint32_t {
  kRngBandwidth     = 1,  // per-partial
  kRngMotionShared  = 2,  // single
  kRngMotionInd     = 3,  // per-partial
  kRngShimmerShared = 4,  // single
  kRngShimmerInd    = 5,  // per-partial
  kRngTrade         = 6,  // single (draw order over pair-walks is stable)
  kRngOnset         = 7,  // single (prepare-time only)
  kRngDetune        = 8,  // single (prepare-time only)
  kRngExpand        = 9,  // single (prepare-time only)
};

// ---------------------------------------------------------------------------
// Partials — abstract base class implementing the per-partial rendering
// engine. Ported faithfully from legacy Partials.cs GetPartialValue().
//
// Owns 5 envelope inputs, rolloff/detune params, expand rule, and all
// static + runtime arrays. Subclasses only implement init_arrays() to
// populate the static arrays (mult1_, mult2_, ampl1_, ampl2_, po1_, po2_).
// ---------------------------------------------------------------------------
struct Partials : ValueSource, IPartials {
  // The additive source drives a Partials through partials_prepare() and
  // partials_next(), never through next(), so the eight envelope pins are
  // Manual: partials_prepare() prepares them and partials_next() advances
  // them, in this order. All are unipolar 0-1 blend factors driving a lerp
  // between _1 and _2 endpoints (e.g. roEnv blends rolloff1 -> rolloff2).
  // Wire an Envelope or RangeSource; a bipolar oscillator won't blend right.
  Param multEnv   {this, "multEnv",    0.0f, 0.0f, 1.0f, "0-1", Pull::Manual};
  Param amplEnv   {this, "amplEnv",    0.0f, 0.0f, 1.0f, "0-1", Pull::Manual};
  Param poEnv     {this, "poEnv",      0.0f, 0.0f, 1.0f, "0-1", Pull::Manual};
  Param roEnv     {this, "roEnv",      0.0f, 0.0f, 1.0f, "0-1", Pull::Manual};
  Param dtEnv     {this, "dtEnv",      0.0f, 0.0f, 1.0f, "0-1", Pull::Manual};
  Param bwEnv     {this, "bwEnv",      0.0f, 0.0f, 1.0f, "0-1", Pull::Manual};  // blends bandwidth1 -> bandwidth2
  Param motionEnv {this, "motionEnv",  0.0f, 0.0f, 1.0f, "0-1", Pull::Manual};  // blends motionDepth1 -> motionDepth2
  Param shimmerEnv{this, "shimmerEnv", 0.0f, 0.0f, 1.0f, "0-1", Pull::Manual};  // blends shimmerDepth1 -> shimmerDepth2

  // Global rolloff/detune ranges
  Setting<float> rolloff1{this, "rolloff1", 1.0f, 0.0f, 10.0f};
  Setting<float> rolloff2{this, "rolloff2", 1.0f, 0.0f, 10.0f};
  Setting<float> detune1 {this, "detune1",  0.0f, 0.0f, 1.0f};
  Setting<float> detune2 {this, "detune2",  0.0f, 0.0f, 1.0f};

  // Bandwidth enhancement: bandwidth1 -> bandwidth2 mix amount (blended by
  // bwEnv), bandwidthHz band width. Default 0 -> feature inert (no change to
  // existing patches).
  Setting<float> bandwidth1 {this, "bandwidth1",  0.0f,  0.0f, 1.0f};
  Setting<float> bandwidth2 {this, "bandwidth2",  0.0f,  0.0f, 1.0f};
  Setting<float> bandwidthHz{this, "bandwidthHz", 30.0f, 1.0f, 2000.0f};

  // Motion layer configs (all default-inert; see partial-motion design spec)
  Setting<float> motionDepth1    {this, "motionDepth1",     0.0f,  0.0f,  400.0f};
  Setting<float> motionDepth2    {this, "motionDepth2",     0.0f,  0.0f,  400.0f};
  Setting<float> motionHz        {this, "motionHz",         4.0f,  0.01f, 200.0f};
  Setting<float> motionCoherence {this, "motionCoherence",  1.0f,  0.0f,  1.0f};
  Setting<float> motionEvolve    {this, "motionEvolve",     0.0f,  0.0f,  1.0f};
  Setting<float> motionScale     {this, "motionScale",      0.0f, -2.0f,  2.0f};
  Setting<float> shimmerDepth1   {this, "shimmerDepth1",    0.0f,  0.0f,  1.0f};
  Setting<float> shimmerDepth2   {this, "shimmerDepth2",    0.0f,  0.0f,  1.0f};
  Setting<float> shimmerHz       {this, "shimmerHz",        3.0f,  0.01f, 200.0f};
  Setting<float> shimmerCoherence{this, "shimmerCoherence", 0.0f,  0.0f,  1.0f};
  Setting<float> shimmerEvolve   {this, "shimmerEvolve",    0.0f,  0.0f,  1.0f};
  Setting<float> shimmerFloor    {this, "shimmerFloor",     0.0f,  0.0f,  1.0f};
  Setting<float> tradeDepth      {this, "tradeDepth",       0.0f,  0.0f,  1.0f};
  Setting<float> tradeHz         {this, "tradeHz",          2.0f,  0.01f, 50.0f};
  Setting<float> onsetSpread     {this, "onsetSpread",      0.0f,  0.0f,  2.0f};
  Setting<float> onsetTilt       {this, "onsetTilt",        0.0f, -1.0f,  1.0f};
  Setting<float> onsetFade       {this, "onsetFade",        0.03f, 0.001f, 1.0f};

  // decayRate/decayExp — per-partial exponential decay law (piano):
  //   rate_i = decayRate * pmult_i^decayExp   [dB/s]
  // i.e. decayRate is the decay rate in dB PER SECOND of a partial at
  // pmult == 1 (the fundamental), and decayExp tilts the law across the
  // spectrum (measured piano within-note exponent ~0.6: high partials die
  // faster). Default 0 = feature off (no per-sample multiply).
  //
  // inharmonicity — stiff-string stretch coefficient B: at prepare/note-on the
  // effective multiplier becomes m*sqrt(1 + B*m^2). Per-note B arrives via a
  // paramMap frequency->curve config entry. Default 0 = off (bit-exact).
  Setting<float> decayRate    {this, "decayRate",     0.0f,  0.0f, 400.0f};
  Setting<float> decayExp     {this, "decayExp",      0.0f, -2.0f, 4.0f};
  Setting<float> inharmonicity{this, "inharmonicity", 0.0f,  0.0f, 0.1f};

  Partials(uint32_t seed = 0xADD2'0000u) : baseSeed_(seed) {}

  // --- ValueSource interface ---
  // Partials is a ValueSource for graph wiring, but doesn't produce audio.
  // Prepare/next delegate to partials_prepare/partials_next.
  void on_prepare(const RenderContext& ctx, int frames) override { partials_prepare(ctx, frames); }
  float next() override { partials_next(); return 0.0f; }
  float current() const override { return 0.0f; }

  const char* type_name() const override { return "Partials"; }
  SourceCategory category() const override { return SourceCategory::Additive; }

  void on_setting_changed(std::string_view name) override {
    if (name == "bandwidthHz" && bandwidthHz.value < 1.0f) bandwidthHz.value = 1.0f;
  }

  // --- Array accessors (for AdditiveSource2 compat) ---
  const std::vector<float>& get_mult1() const { return mult1_; }
  const std::vector<float>& get_mult2() const { return mult2_; }
  const std::vector<float>& get_ampl1() const { return ampl1_; }
  const std::vector<float>& get_ampl2() const { return ampl2_; }

  // --- Set by the patch loader from an "expandRule" block ---
  void set_expand_rule(ExpandRule rule) { expandRule_ = rule; hasExpand_ = true; }

  // --- IPartials interface ---

  void partials_prepare(const RenderContext& ctx, int frames) override {
    rate_ = float(ctx.sampleRate);

    // Per-note stream base: baseSeed_ hashed with a note counter. Every rng
    // stream below reseeds from this, so a note's randomness depends only on
    // (baseSeed, note ordinal, partial, layer) — never on how many draws any
    // earlier code path happened to make. The note counter preserves the old
    // per-note variety (the shared stream used to just continue across
    // note-ons); without it every note would replay identical noise.
    noteBase_ = uint32_t(splitmix64((uint64_t(baseSeed_) << 32) ^ uint64_t(noteSeq_)));
    ++noteSeq_;

    if (arrayUpdateReq_) {
      update_arrays();
    }

    if (multEnv.source)    multEnv.source->prepare(ctx, frames);
    if (amplEnv.source)    amplEnv.source->prepare(ctx, frames);
    if (poEnv.source)      poEnv.source->prepare(ctx, frames);
    if (roEnv.source)      roEnv.source->prepare(ctx, frames);
    if (dtEnv.source)      dtEnv.source->prepare(ctx, frames);
    if (bwEnv.source)      bwEnv.source->prepare(ctx, frames);
    if (motionEnv.source)  motionEnv.source->prepare(ctx, frames);
    if (shimmerEnv.source) shimmerEnv.source->prepare(ctx, frames);

    // Expand partials if rule is set
    if (hasExpand_) {
      if (origMult1_.empty()) {
        origMult1_ = mult1_; origMult2_ = mult2_;
        origAmpl1_ = ampl1_; origAmpl2_ = ampl2_;
        origPo1_ = po1_; origPo2_ = po2_;
      } else {
        mult1_ = origMult1_; mult2_ = origMult2_;
        ampl1_ = origAmpl1_; ampl2_ = origAmpl2_;
        po1_ = origPo1_; po2_ = origPo2_;
      }
      expandRng_ = Randomizer(stream_seed(noteBase_, 0, kRngExpand));
      for (int r = 0; r <= expandRule_.recurse; ++r)
        apply_expand_rule();
    }

    int n = int(mult1_.size());
    partialPos_.assign(n, 0.0f);
    partialPO_.assign(n, 0.0f);
    partialLPO_.assign(n, 0.0f);

    // Per-partial caches (see ensure_partial_cache). Sized here — the render
    // loop never allocates. Invalidated so the first sample rebuilds them
    // against this note's array contents.
    pmultCache_.assign(n, 0.0f);
    rolloffCache_.assign(n, 1.0f);
    moScaleCache_.assign(n, 1.0f);
    partialCacheValid_ = false;

    // Per-partial decay: running gain state, reset to unity at note-on.
    // decayGain_ (the per-sample factor) is filled by ensure_partial_cache
    // alongside pmultCache_ — it depends on the effective (possibly
    // inharmonicity-stretched) multiplier. Sized here; render never allocates.
    decayActive_ = (decayRate.value != 0.0f);
    if (decayActive_) {
      decayGain_.assign(n, 1.0f);
      decayState_.assign(n, 1.0f);
    }

    // Per-partial bandwidth-noise state. Each partial gets an INDEPENDENT
    // smoothed random walk (decorrelated start phase + targets) so the bands
    // fill in rather than wobbling in unison. Segment length = rate/bandwidthHz.
    bwLen_ = std::max(1, int(rate_ / (bandwidthHz.value < 1.0f ? 1.0f : bandwidthHz.value)));
    bwCur_.assign(n, 0.0f);
    bwTarget_.assign(n, 0.0f);
    bwPos_.assign(n, 0);
    if (int(bwRng_.size()) != n) bwRng_.resize(n, Randomizer(0u));
    for (int i = 0; i < n; ++i) {
      bwRng_[i]    = Randomizer(stream_seed(noteBase_, uint32_t(i), kRngBandwidth));
      bwCur_[i]    = bwRng_[i].valuePN();
      bwTarget_[i] = bwRng_[i].valuePN();
      bwPos_[i]    = int(bwRng_[i].range(0.0f, float(bwLen_)));  // stagger phase
    }

    // Motion / shimmer / trade / onset state. All per-note (prepare fires at
    // every note-on) and inert unless configured. Allocation happens here,
    // never in the per-sample path.
    sampleIdx_ = 0;
    motionActive_ = (motionDepth1.value != 0.0f || motionDepth2.value != 0.0f);
    if (motionActive_) {
      moLen_ = std::max(1, int(rate_ / std::max(0.01f, motionHz.value)));
      moSharedRng_ = Randomizer(stream_seed(noteBase_, 0, kRngMotionShared));
      walk_init(moShared_, moLen_, motionEvolve.value, moSharedRng_);
      moWalks_.assign(n, MotionWalk{});
      if (int(moRng_.size()) != n) moRng_.resize(n, Randomizer(0u));
      for (int i = 0; i < n; ++i) {
        moRng_[i] = Randomizer(stream_seed(noteBase_, uint32_t(i), kRngMotionInd));
        walk_init(moWalks_[i], moLen_, motionEvolve.value, moRng_[i]);
      }
      moVals_.assign(n, 0.0f);
    }
    shimmerActive_ = (shimmerDepth1.value != 0.0f || shimmerDepth2.value != 0.0f);
    if (shimmerActive_) {
      shLen_ = std::max(1, int(rate_ / std::max(0.01f, shimmerHz.value)));
      shSharedRng_ = Randomizer(stream_seed(noteBase_, 0, kRngShimmerShared));
      walk_init(shShared_, shLen_, shimmerEvolve.value, shSharedRng_);
      shWalks_.assign(n, MotionWalk{});
      if (int(shRng_.size()) != n) shRng_.resize(n, Randomizer(0u));
      for (int i = 0; i < n; ++i) {
        shRng_[i] = Randomizer(stream_seed(noteBase_, uint32_t(i), kRngShimmerInd));
        walk_init(shWalks_[i], shLen_, shimmerEvolve.value, shRng_[i]);
      }
      shVals_.assign(n, 0.0f);
    }
    tradeActive_ = (tradeDepth.value != 0.0f);
    if (tradeActive_) {
      trLen_ = std::max(1, int(rate_ / std::max(0.01f, tradeHz.value)));
      trRng_ = Randomizer(stream_seed(noteBase_, 0, kRngTrade));
      trWalks_.assign((n + 1) / 2, MotionWalk{});
      for (auto& w : trWalks_) walk_init(w, trLen_, 0.0f, trRng_);
      trVals_.assign((n + 1) / 2, 0.0f);
    }
    onsetActive_ = (onsetSpread.value > 0.0f);
    if (onsetActive_) {
      // Per-partial onset delay: tilt orders delays by partial height
      // (+1 low-first / high partials bloom later, -1 reverse, 0 random).
      Randomizer onsetRng(stream_seed(noteBase_, 0, kRngOnset));
      onsetDelay_.resize(n);
      float at = std::fabs(onsetTilt.value);
      for (int i = 0; i < n; ++i) {
        float u = onsetRng.value();
        float r = (n > 1) ? float(i) / float(n - 1) : 0.0f;
        float ordered = (onsetTilt.value >= 0.0f) ? r : 1.0f - r;
        onsetDelay_[i] = ((1.0f - at) * u + at * ordered) * onsetSpread.value * rate_;
      }
      onsetFadeSamples_ = std::max(1.0f, onsetFade.value * rate_);
    }

    init_detune_values();

    // Seed the per-sample scalars so a get_partial_value() before the first
    // partials_next() (e.g. a UI probe) never reads stale values.
    refresh_sample_scalars();
  }

  void partials_next() override {
    multEnv.advance();
    amplEnv.advance();
    poEnv.advance();
    roEnv.advance();
    dtEnv.advance();
    bwEnv.advance();
    motionEnv.advance();
    shimmerEnv.advance();

    refresh_sample_scalars();

    ++sampleIdx_;
    if (motionActive_) {
      float shared = walk_advance(moShared_, moLen_, motionEvolve.value, moSharedRng_);
      float coh = motionCoherence.value;
      for (size_t i = 0; i < moWalks_.size(); ++i) {
        float ind = walk_advance(moWalks_[i], moLen_, motionEvolve.value, moRng_[i]);
        moVals_[i] = coh * shared + (1.0f - coh) * ind;
      }
    }
    if (shimmerActive_) {
      float shared = walk_advance(shShared_, shLen_, shimmerEvolve.value, shSharedRng_);
      float coh = shimmerCoherence.value;
      for (size_t i = 0; i < shWalks_.size(); ++i) {
        float ind = walk_advance(shWalks_[i], shLen_, shimmerEvolve.value, shRng_[i]);
        shVals_[i] = coh * shared + (1.0f - coh) * ind;
      }
    }
    if (tradeActive_) {
      for (size_t i = 0; i < trWalks_.size(); ++i)
        trVals_[i] = walk_advance(trWalks_[i], trLen_, 0.0f, trRng_);
    }
    if (decayActive_) {
      // Advance each partial's running decay gain by its per-sample factor.
      // ensure_partial_cache first: decayGain_ is derived there from the
      // effective multiplier (cheap no-op once the cache is valid).
      ensure_partial_cache();
      const size_t n = decayState_.size();
      for (size_t i = 0; i < n; ++i) decayState_[i] *= decayGain_[i];
    }
  }

  int partial_count() const override {
    return int(mult1_.size());
  }

  // Port of legacy Partials.cs GetPartialValue — the authoritative per-partial
  // math. Kept as the IPartials entry point for single-partial callers; the
  // render path goes through sum_partials() below, which hoists everything
  // that is invariant across partials out of the loop.
  float get_partial_value(float amplitude, float frequency, float phaseDiff,
                          int index, IFormant* formant, float fmtWt,
                          float fmtFloor) override
  {
    ensure_partial_cache();
    return partial_value_impl(amplitude, frequency, phaseDiff, index,
                              formant, fmtWt, fmtFloor);
  }

  // Batched per-sample sum. Same math, same accumulation order as the loop
  // FullAdditiveSource used to run — renders are byte-identical.
  float sum_partials(float acc, float amplitude, float frequency,
                     float phaseDiff, IFormant* formant, float fmtWt,
                     float fmtFloor) override
  {
    ensure_partial_cache();
    const int n = int(mult1_.size());
    for (int i = 0; i < n; ++i) {
      float v = partial_value_impl(amplitude, frequency, phaseDiff, i,
                                   formant, fmtWt, fmtFloor);
      if (std::isnan(v)) continue;   // past cutoff — gate this partial only
      acc += v;
    }
    return acc;
  }

private:
  // The per-partial body. Reads the per-sample scalars cached by
  // refresh_sample_scalars() and the per-partial values cached by
  // ensure_partial_cache() instead of re-deriving them N times per sample.
  inline float partial_value_impl(float amplitude, float frequency, float phaseDiff,
                                  int index, IFormant* formant, float fmtWt,
                                  float fmtFloor)
  {
    // Multiplier (can evolve between mult1 and mult2) — cached on multE.
    float pmult = pmultCache_[index];

    // Phase offset (can evolve)
    float ppo = po1_[index] + (po2_[index] - po1_[index]) * sPoE_;

    // Frequency = multiplier * base freq * (1 + detune)
    float dt = sDt_ * dtVals_[index];
    float pfreq = pmult * frequency * (1.0f + dt);

    // Frequency motion: cents offset from the coherence-mixed random walk.
    // Coherent component moves all partials by the same cents (proportional
    // Hz — fuses like vibrato); independent component broadens lines.
    if (motionActive_) {
      if (sMd_ != 0.0f) {
        float cents = sMd_ * moVals_[index];
        if (motionScale.value != 0.0f) cents *= moScaleCache_[index];
        // fast_exp2, not std::exp2: this is a CRT call executed once per
        // partial per sample, and it measured at 65.6% of viola_default's
        // entire loop cost (tools/ablate_layers.py). The replacement is
        // accurate to 0.88 float32 eps — see core/fast_math.h.
        // std::exp2, not fast_exp2: the last-bit difference flips borderline
        // cutoff comparisons, which used to re-deal the shared rng stream and
        // audibly re-roll high-note bandwidth patches (run-14 A/B, Matt
        // verdict: revert). Per-partial rng streams have since made noise
        // order-independent, so a cutoff flip now only gates that one partial
        // — fast_exp2 is reinstatable, but the swap is its own A/B decision.
        pfreq *= std::exp2(cents * (1.0f / 1200.0f));
      }
    }

    // Past cutoff -> NaN signal to caller
    if (pfreq > CUTOFF) return std::numeric_limits<float>::quiet_NaN();

    // Advance partial position (legacy Partials.cs lines 197-200).
    // fmod(x, 1) == x - trunc(x) exactly for the |x| < 2 range this sees
    // (pfreq/rate <= 1/3, phaseDiff and the po delta are small).
    //
    // truncf was expected to be a single instruction; it is not. Under the
    // engine's Release flags (/O2, SSE2 baseline, no /arch) MSVC emits a CRT
    // CALL for std::truncf — roundss is SSE4.1 and therefore off the table —
    // so this cost a function call per partial per sample. The int round-trip
    // is one cvttss2si + one cvtsi2ss and is BIT-IDENTICAL to truncf for every
    // |x| < 2^31, which this is by many orders of magnitude (pfreq is capped
    // at CUTOFF just above, so pfreq/rate <= 1/3).
    // Measured in isolation on synthetic arrays, 1.4-1.5x on the whole loop
    // body: research/additive_perf/simd_proto.cpp, rung "sc+cast".
    {
      // ANTI-RESULT (run 14, don't retry): replacing this divide with a
      // multiply by a cached 1/rate_ is a WASH-TO-SLOWER in the engine —
      // marginal 25.0 -> 29.6 ns/sample/partial on the 32/200 ladder, 0.89x
      // at 200 partials — even though the same substitution measured 1.10x in
      // the isolated prototype. The divide is not on the critical path here;
      // the cached reciprocal costs a load the immediate didn't. It also
      // costs bit-exactness (1/48000 is not representable), so it was
      // rejected on both counts.
      float x = partialPos_[index] + pfreq / rate_ + phaseDiff
              + (ppo - partialLPO_[index]);
      x = x - float(int(x));
      if (x < 0.0f) x += 1.0f;
      partialPos_[index] = x;
    }
    partialLPO_[index] = ppo;

    // Amplitude with rolloff (legacy lines 206-216) — cached on (multE, roE).
    float rolloff = rolloffCache_[index];

    // Formant factor — additive-boost with out-of-band floor.
    //   in-band:      factor = fmtFloor + fmtWt * gain-at-frequency
    //   out-of-band:  factor = fmtFloor
    // fmtWt is the "resonance amount" knob (0 = flat, higher = stronger
    // peaks). fmtFloor is the inter-formant suppression: at 1.0 (default)
    // this reproduces the pure additive-boost semantic exactly — out-of-band
    // partials pass at unity and band edges (gain -> 0) are continuous at
    // 1.0. Lowering fmtFloor CUTS the inter-formant regions (0.05-0.15 with
    // fmtWt ~1 drops them 16-26 dB while formant peaks stay near/above
    // unity) — the missing half of vowel character; boost-only formants
    // read as a buzzy sawtooth. Continuity at the band edges holds for any
    // fmtFloor since gain -> 0 there.
    float fmtFactor = 1.0f;
    if (formant) {
      fmtFactor = formant->contains(pfreq)
          ? fmtFloor + fmtWt * formant->get_gain(pfreq)
          : fmtFloor;
    }

    // Fade out partials near cutoff (within 1000 Hz) — legacy line 216
    float fade = (pfreq < CUTOFF - 1000.0f) ? 1.0f : (CUTOFF - pfreq) / 1000.0f;

    float pampl = amplitude *
        (ampl1_[index] + (ampl2_[index] - ampl1_[index]) * sAmplE_) *
        rolloff * fmtFactor * fade;

    // Per-partial exponential decay (piano): one multiply by the running
    // gain advanced in partials_next(). Inert at decayRate == 0.
    if (decayActive_) pampl *= decayState_[index];

    // Bandwidth enhancement (Loris / SMS): trade part of this partial's
    // sinusoidal energy for a noise band by amplitude-modulating it with an
    // independent per-partial smoothed noise. Energy-preserving mix:
    //   amp *= sqrt(1-bw) + sqrt(bw)*noise   (bw=0 → pure sine; bw=1 → full band)
    // bandwidthHz sets how fast the noise wiggles ≈ the band width. bwEnv lets
    // bandwidth ramp (e.g. high at the attack, settling — a coupled noisy attack).
    if (sBwActive_) {
      if (bwPos_[index] >= bwLen_) {
        bwCur_[index]    = bwTarget_[index];
        bwTarget_[index] = bwRng_[index].valuePN();  // per-partial stream
        bwPos_[index]    = 0;
      }
      float u = float(bwPos_[index]) / float(bwLen_);
      float s = u * u * (3.0f - 2.0f * u);  // smoothstep → low-pass noise
      float noise = bwCur_[index] + (bwTarget_[index] - bwCur_[index]) * s;
      ++bwPos_[index];
      pampl *= sBwDry_ + sBwWet_ * noise;   // sqrt(1-bw), sqrt(bw): per-sample
    }

    // Amplitude shimmer: slow per-partial gain wander (coherence-mixed walk).
    // shimmerFloor clamps the gain from below so high depths can't visit
    // near-silence (measured 14.6 dB mid-note dips at depth ~1.09); the
    // default 0 reproduces the old max(0, g) exactly.
    if (shimmerActive_) {
      float g = 1.0f + sSd_ * shVals_[index];
      pampl *= (g < shimmerFloor.value ? shimmerFloor.value : g);
    }

    // Energy trading: adjacent pairs share one walk with opposite signs, so
    // energy sloshes between neighbors while the pair sum stays ~constant.
    if (tradeActive_) {
      float g = 1.0f + ((index & 1) ? -tradeDepth.value : tradeDepth.value) * trVals_[index >> 1];
      pampl *= (g < 0.0f ? 0.0f : g);
    }

    // Onset dispersion: partial stays silent until its per-note delay, then
    // fades in. Phase keeps advancing while gated, so partials "enter" as
    // already-running oscillators rather than all striking at t=0.
    if (onsetActive_) {
      float t = (float(sampleIdx_) - onsetDelay_[index]) / onsetFadeSamples_;
      if (t <= 0.0f) pampl = 0.0f;
      else if (t < 1.0f) pampl *= t * t * (3.0f - 2.0f * t);
    }

    // fast_sin_turns takes the phase directly in turns — no TAU multiply, no
    // libm range reduction. See fast_math.h for the accuracy bound.
    return fast_sin_turns(partialPos_[index]) * pampl;
  }

  // --- per-sample scalar cache -------------------------------------------
  // Every value here is identical for all partials of a sample; reading them
  // once per sample instead of once per partial removes N virtual calls.
  // Expressions and operand order match the originals exactly, so the values
  // are bit-identical to what the inline versions produced.
  void refresh_sample_scalars() {
    sMultE_ = multEnv.current();
    sAmplE_ = amplEnv.current();
    sPoE_   = poEnv.current();
    sRoE_   = roEnv.current();
    sDtE_   = dtEnv.current();

    sDt_ = detune1.value + (detune2.value - detune1.value) * sDtE_;
    sMd_ = motionDepth1.value + (motionDepth2.value - motionDepth1.value) * motionEnv.current();
    sSd_ = shimmerDepth1.value + (shimmerDepth2.value - shimmerDepth1.value) * shimmerEnv.current();

    float bw = bandwidth1.value + (bandwidth2.value - bandwidth1.value) * bwEnv.current();
    sBwActive_ = (bw > 0.0f);
    if (sBwActive_) {
      if (bw > 1.0f) bw = 1.0f;
      sBwDry_ = std::sqrt(1.0f - bw);
      sBwWet_ = std::sqrt(bw);
    }
  }

  // --- per-partial cache --------------------------------------------------
  // pmult, rolloff and the motion frequency-scale factor depend only on the
  // scalars multE and roE. Rebuild the arrays when either changes (exact float
  // compare); otherwise reuse. pow() is deterministic, so a reused value is
  // bit-identical to a recomputed one. Constant multEnv/roEnv — every patch in
  // the repo today — means one rebuild per note and none per sample.
  void ensure_partial_cache() {
    if (partialCacheValid_ && sMultE_ == cachedMultE_ && sRoE_ == cachedRoE_)
      return;
    const int n = int(mult1_.size());
    const float ro = rolloff1.value + (rolloff2.value - rolloff1.value) * sRoE_;
    const bool scale = (motionScale.value != 0.0f);
    const bool inharm = (inharmonicity.value != 0.0f);
    for (int i = 0; i < n; ++i) {
      float pmult = mult1_[i] + (mult2_[i] - mult1_[i]) * sMultE_;
      // Stiff-string stretch: m -> m*sqrt(1 + B*m^2). Applied to the
      // effective multiplier so frequency, rolloff and the decay law all see
      // the stretched partial. Branch keeps B == 0 bit-exact.
      if (inharm) pmult *= std::sqrt(1.0f + inharmonicity.value * pmult * pmult);
      pmultCache_[i]   = pmult;
      rolloffCache_[i] = (ro == 0.0f) ? 1.0f : (1.0f / std::pow(pmult, ro));
      if (scale) moScaleCache_[i] = std::pow(pmult, motionScale.value);
      if (decayActive_) {
        // rate_i = decayRate * pmult^decayExp dB/s -> per-sample amplitude
        // factor 10^(-rate_i / (20*sr)). Double intermediates: the factor is
        // within ~1e-7 of 1, where float pow quantisation would bias the
        // long-run decay audibly.
        const double rate = double(decayRate.value) *
                            std::pow(double(pmult), double(decayExp.value));
        decayGain_[i] = float(std::pow(10.0, -rate / (20.0 * double(rate_))));
      }
    }
    cachedMultE_ = sMultE_;
    cachedRoE_   = sRoE_;
    partialCacheValid_ = true;
  }

public:
  // Read-only access to the live partial arrays. Subclasses (FullPartials,
  // SequencePartials) populate these via init_arrays(). ExplicitPartials
  // overrides to prefer its user-edited Stat_ copies. Used by the UI's
  // partials strip view to render the bar chart.
  //
  // Triggers an array rebuild if a config change marked them dirty — without
  // this, configs set after construction (e.g. evenWeight2=0 from JSON load)
  // wouldn't take effect until the first render call partials_prepare().
  std::vector<float> get_array(std::string_view name) const override {
    if (arrayUpdateReq_) const_cast<Partials*>(this)->update_arrays();
    if (name == "mult1") return mult1_;
    if (name == "mult2") return mult2_;
    if (name == "ampl1") return ampl1_;
    if (name == "ampl2") return ampl2_;
    return {};
  }

protected:
  // Subclass populates mult1_, mult2_, ampl1_, ampl2_, po1_, po2_
  virtual void init_arrays() {}

  void update_arrays() {
    init_arrays();
    arrayUpdateReq_ = false;
  }

  void init_detune_values() {
    Randomizer dtRng(stream_seed(noteBase_, 0, kRngDetune));
    int n = int(mult1_.size());
    dtVals_.resize(n);
    for (int i = 0; i < n; ++i) {
      // Legacy: sign < 0 -> range(-0.5, 0), else -> range(0, 1)
      dtVals_[i] = (dtRng.sign() < 0) ? dtRng.range(-0.5f, 0.0f) : dtRng.range(0.0f, 1.0f);
    }
  }

  void apply_expand_rule() {
    const auto& er = expandRule_;
    int origN = int(mult1_.size());
    int newN = origN * (er.count * 2 + 1);

    std::vector<float> m1(newN), m2(newN), a1(newN), a2(newN), p1(newN), p2(newN);

    for (int i = 0; i < origN; ++i) {
      int base = i * (er.count * 2 + 1);

      // Left sub-partials
      for (int j = 0; j < er.count; ++j) {
        int idx = base + j;
        float semis1 = float(er.count - j) * er.spacing1;
        float semis2 = float(er.count - j) * er.spacing2;

        m1[idx] = mult1_[i] / std::pow(2.0f, semis1 / 12.0f) * (1.0f + expandRng_.valuePN() * er.dt1);
        m2[idx] = mult2_[i] / std::pow(2.0f, semis2 / 12.0f) * (1.0f + expandRng_.valuePN() * er.dt2);

        float t = float(j) / float(er.count);
        a1[idx] = ampl1_[i] * er.loPct1 + ampl1_[i] * (1.0f - er.loPct1) * std::pow(t, er.power1);
        a2[idx] = ampl2_[i] * er.loPct2 + ampl2_[i] * (1.0f - er.loPct2) * std::pow(t, er.power2);

        p1[idx] = std::fmod(po1_[i] + float(j + 1) / float(er.count) * er.po1, 1.0f);
        p2[idx] = std::fmod(po2_[i] + float(j + 1) / float(er.count) * er.po2, 1.0f);
      }

      // Primary (center)
      int cIdx = base + er.count;
      m1[cIdx] = mult1_[i]; m2[cIdx] = mult2_[i];
      a1[cIdx] = ampl1_[i]; a2[cIdx] = ampl2_[i];
      p1[cIdx] = po1_[i];   p2[cIdx] = po2_[i];

      // Right sub-partials
      for (int j = 0; j < er.count; ++j) {
        int idx = base + er.count + 1 + j;
        float semis1 = float(j + 1) * er.spacing1;
        float semis2 = float(j + 1) * er.spacing2;

        m1[idx] = mult1_[i] * std::pow(2.0f, semis1 / 12.0f);
        m2[idx] = mult2_[i] * std::pow(2.0f, semis2 / 12.0f);

        float t = float(er.count - j - 1) / float(er.count);
        a1[idx] = ampl1_[i] * er.loPct1 + ampl1_[i] * (1.0f - er.loPct1) * std::pow(t, er.power1);
        a2[idx] = ampl2_[i] * er.loPct2 + ampl2_[i] * (1.0f - er.loPct2) * std::pow(t, er.power2);

        p1[idx] = std::fmod(po1_[i] + float(er.count - j) / float(er.count) * er.po1, 1.0f);
        p2[idx] = std::fmod(po2_[i] + float(er.count - j) / float(er.count) * er.po2, 1.0f);
      }
    }

    mult1_ = std::move(m1); mult2_ = std::move(m2);
    ampl1_ = std::move(a1); ampl2_ = std::move(a2);
    po1_   = std::move(p1); po2_   = std::move(p2);
  }

  // Smoothed random walk with meta-randomized segments — the primitive under
  // the motion/shimmer/trade layers. evolve > 0 makes each new segment
  // re-roll its own length and excursion, so the movement is non-stationary
  // ("the character of the movement itself changes over time").
  struct MotionWalk {
    float cur{0.0f}, target{0.0f};
    int pos{0}, len{1};
  };

  int walk_seg_len(int baseLen, float evolve, Randomizer& rng) {
    float f = (evolve <= 0.0f) ? 1.0f
                               : std::pow(1.0f + 3.0f * evolve, rng.valuePN());
    int len = int(float(baseLen) * f);
    return len < 1 ? 1 : len;
  }

  void walk_init(MotionWalk& w, int baseLen, float evolve, Randomizer& rng) {
    w.cur = rng.valuePN();
    w.target = rng.valuePN();
    w.len = walk_seg_len(baseLen, evolve, rng);
    w.pos = int(rng.range(0.0f, float(w.len)));  // stagger segment phase
  }

  float walk_advance(MotionWalk& w, int baseLen, float evolve, Randomizer& rng) {
    if (w.pos >= w.len) {
      w.cur = w.target;
      float amp = 1.0f + 0.7f * evolve * rng.valuePN();
      w.target = rng.valuePN() * amp;
      w.len = walk_seg_len(baseLen, evolve, rng);
      w.pos = 0;
    }
    float u = float(w.pos) / float(w.len);
    float s = u * u * (3.0f - 2.0f * u);  // smoothstep → low-pass movement
    ++w.pos;
    return w.cur + (w.target - w.cur) * s;
  }

  // Rng streams — one per (partial, layer) for the layers with per-partial
  // state, one per layer otherwise. All reseeded per note in partials_prepare
  // from stream_seed(noteBase_, index, layerId); nothing in the render path
  // shares a stream. baseSeed_ comes from the node's JSON "seed".
  uint32_t baseSeed_;
  uint32_t noteSeq_{0};   // note-on ordinal, hashed into noteBase_
  uint32_t noteBase_{0};
  Randomizer moSharedRng_{0u}, shSharedRng_{0u}, trRng_{0u}, expandRng_{0u};
  std::vector<Randomizer> bwRng_, moRng_, shRng_;  // per-partial streams

  // Bandwidth-noise segment length in samples (derived from bandwidthHz at prepare)
  int   bwLen_{1600};

  // Motion layer runtime state (allocated in partials_prepare)
  bool motionActive_{false}, shimmerActive_{false};
  bool tradeActive_{false}, onsetActive_{false};
  bool decayActive_{false};
  std::vector<float> decayGain_, decayState_;  // per-sample factor / running gain
  int moLen_{1}, shLen_{1}, trLen_{1};
  MotionWalk moShared_, shShared_;
  std::vector<MotionWalk> moWalks_, shWalks_, trWalks_;
  std::vector<float> moVals_, shVals_, trVals_;
  std::vector<float> onsetDelay_;
  float onsetFadeSamples_{1.0f};
  int sampleIdx_{0};

  // Per-partial static arrays (set by init_arrays in subclass)
  std::vector<float> mult1_, mult2_, ampl1_, ampl2_, po1_, po2_;

  // Per-partial runtime state
  std::vector<float> partialPos_, partialPO_, partialLPO_, dtVals_;
  std::vector<float> bwCur_, bwTarget_;   // per-partial bandwidth-noise walk
  std::vector<int>   bwPos_;

  // Per-sample scalars, refreshed once per partials_next() (see
  // refresh_sample_scalars) instead of once per partial.
  float sMultE_{0.0f}, sAmplE_{0.0f}, sPoE_{0.0f}, sRoE_{0.0f}, sDtE_{0.0f};
  float sDt_{0.0f}, sMd_{0.0f}, sSd_{0.0f};
  bool  sBwActive_{false};
  float sBwDry_{1.0f}, sBwWet_{0.0f};

  // Per-partial caches keyed on (multE, roE) — see ensure_partial_cache.
  std::vector<float> pmultCache_, rolloffCache_, moScaleCache_;
  float cachedMultE_{0.0f}, cachedRoE_{0.0f};
  bool  partialCacheValid_{false};

  // Expand rule
  ExpandRule expandRule_;
  bool hasExpand_{false};
  std::vector<float> origMult1_, origMult2_, origAmpl1_, origAmpl2_, origPo1_, origPo2_;

  bool arrayUpdateReq_{true};

  // Sample rate captured from ctx at partials_prepare(); drives partial phase
  // advance in get_partial_value(). Previously a hardcoded 48000 constexpr.
  float rate_{48000.0f};

  static constexpr float CUTOFF = 16000.0f;
};

// ---------------------------------------------------------------------------
// FullPartials — integer harmonics 1..N with even/odd weight control.
// Ported from legacy FullPartials.cs InitArrays.
// ---------------------------------------------------------------------------
struct FullPartials final : Partials {
  Setting<int>   maxPartials{this, "maxPartials", 30, 1, 200};
  Setting<int>   minMult    {this, "minMult",     1,  1, 100};
  Setting<float> evenWeight1{this, "evenWeight1", 1.0f, 0.0f, 10.0f};
  Setting<float> evenWeight2{this, "evenWeight2", 1.0f, 0.0f, 10.0f};
  Setting<float> oddWeight1 {this, "oddWeight1",  1.0f, 0.0f, 10.0f};
  Setting<float> oddWeight2 {this, "oddWeight2",  1.0f, 0.0f, 10.0f};
  Setting<float> unitPO1    {this, "unitPO1",     0.0f, 0.0f, 1.0f};
  Setting<float> unitPO2    {this, "unitPO2",     0.0f, 0.0f, 1.0f};

  FullPartials(uint32_t seed = 0xADD2'0000u) : Partials(seed) { init_arrays(); }

  const char* type_name() const override { return "FullPartials"; }

  // Each of these settings reshapes the arrays, which are rebuilt at the next prepare.
  void on_setting_changed(std::string_view name) override {
    Partials::on_setting_changed(name);
    if (name == "maxPartials") maxPartials.value = std::max(1, maxPartials.value);
    if (name == "minMult")     minMult.value = std::max(1, minMult.value);
    for (const char* own : {"maxPartials", "minMult", "evenWeight1", "evenWeight2",
                            "oddWeight1", "oddWeight2", "unitPO1", "unitPO2"})
      if (name == own) arrayUpdateReq_ = true;
  }

protected:
  void init_arrays() override {
    int n = maxPartials.value;
    mult1_.resize(n); mult2_.resize(n);
    ampl1_.resize(n); ampl2_.resize(n);
    po1_.resize(n);   po2_.resize(n);

    for (int i = 0; i < n; ++i) {
      float m = float(minMult.value + i);
      mult1_[i] = m;

      // Even/odd amplitude weights (fundamental always 1) — legacy FullPartials.cs
      if (int(std::trunc(m)) % 2 == 0) {
        ampl1_[i] = evenWeight1.value;
        ampl2_[i] = evenWeight2.value;
      } else {
        ampl1_[i] = (m == 1.0f) ? 1.0f : oddWeight1.value;
        ampl2_[i] = (m == 1.0f) ? 1.0f : oddWeight2.value;
      }

      // Phase offsets: (mult - 1) * unitPO
      po1_[i] = std::fmod((m - 1.0f) * unitPO1.value, 1.0f);
      po2_[i] = std::fmod((m - 1.0f) * unitPO2.value, 1.0f);
    }

    // No multiplier evolution for FullPartials
    mult2_ = mult1_;
  }
};

// ---------------------------------------------------------------------------
// SequencePartials — linear multiplier sequences with evolving spacing.
// Ported from legacy SequencePartials.cs InitArrays.
// ---------------------------------------------------------------------------
struct SequencePartials final : Partials {
  Setting<int>   maxPartials{this, "maxPartials", 30, 1, 200};
  Setting<float> minMult1   {this, "minMult1",    1.0f, 0.01f, 100.0f};
  Setting<float> minMult2   {this, "minMult2",    1.0f, 0.01f, 100.0f};
  Setting<float> incr1      {this, "incr1",       1.0f, 0.01f, 100.0f};
  Setting<float> incr2      {this, "incr2",       1.0f, 0.01f, 100.0f};
  Setting<float> unitPO1    {this, "unitPO1",     0.0f, 0.0f,  1.0f};
  Setting<float> unitPO2    {this, "unitPO2",     0.0f, 0.0f,  1.0f};

  SequencePartials(uint32_t seed = 0xADD2'0000u) : Partials(seed) { init_arrays(); }

  const char* type_name() const override { return "SequencePartials"; }

  // Each of these settings reshapes the arrays, which are rebuilt at the next prepare.
  void on_setting_changed(std::string_view name) override {
    Partials::on_setting_changed(name);
    if (name == "maxPartials") maxPartials.value = std::max(1, maxPartials.value);
    for (const char* own : {"maxPartials", "minMult1", "minMult2", "incr1", "incr2",
                            "unitPO1", "unitPO2"})
      if (name == own) arrayUpdateReq_ = true;
  }

protected:
  void init_arrays() override {
    int n = maxPartials.value;
    mult1_.resize(n); mult2_.resize(n);
    ampl1_.assign(n, 1.0f); ampl2_.assign(n, 1.0f);
    po1_.resize(n); po2_.resize(n);

    for (int i = 0; i < n; ++i) {
      mult1_[i] = minMult1.value + float(i) * incr1.value;
      mult2_[i] = minMult2.value + float(i) * incr2.value;
      po1_[i] = std::fmod((mult1_[i] - 1.0f) * unitPO1.value, 1.0f);
      po2_[i] = std::fmod((mult2_[i] - 1.0f) * unitPO2.value, 1.0f);
    }
  }
};

// ---------------------------------------------------------------------------
// ExplicitPartials — user-specified multiplier and amplitude arrays.
// Ported from legacy ExplicitPartials.cs InitArrays.
// ---------------------------------------------------------------------------
struct ExplicitPartials final : Partials {
  Setting<int>   maxPartials{this, "maxPartials", 16, 1, 200};
  Setting<bool>  evolve     {this, "evolve",      true, false, true};
  Setting<float> unitPO1    {this, "unitPO1",     0.0f, 0.0f, 1.0f};
  Setting<float> unitPO2    {this, "unitPO2",     0.0f, 0.0f, 1.0f};

  // Four parallel arrays grouped as "partials" — the UI table keeps them equal
  // length. Column order pairs start/end per partial: Mult1, Ampl1, Mult2, Ampl2.
  // When evolve=false, _2 columns are kept mirrored from _1 (UI disables them).
  // These are the user's copies; init_arrays() derives the working arrays (and
  // the phase offsets) from them, and again after an expansion.
  Array mult1{this, "mult1", "partials", 1.0f, 0.0f, 200.0f};
  Array ampl1{this, "ampl1", "partials", 1.0f, 0.0f, 10.0f};
  Array mult2{this, "mult2", "partials", 1.0f, 0.0f, 200.0f};
  Array ampl2{this, "ampl2", "partials", 1.0f, 0.0f, 10.0f};

  ExplicitPartials(uint32_t seed = 0xADD2'0000u) : Partials(seed) { init_arrays_defaults(); }

  const char* type_name() const override { return "ExplicitPartials"; }

  void on_setting_changed(std::string_view name) override {
    Partials::on_setting_changed(name);
    if (name == "maxPartials") {
      maxPartials.value = std::max(1, maxPartials.value);
      init_arrays_defaults();
    } else if (name == "evolve" && !evolve.value) {
      // When turning evolve off, mirror _1 -> _2 so rendering stops drifting.
      mult2.values = mult1.values;
      ampl2.values = ampl1.values;
      init_arrays();
    }
  }

  void on_array_changed(std::string_view name) override {
    if (!evolve.value) {
      if (name == "mult1") mult2.values = mult1.values;
      if (name == "ampl1") ampl2.values = ampl1.values;
    }
    maxPartials.value = int(mult1.values.size());
    init_arrays();
  }

  // Falls back to the working arrays (init_arrays_defaults populates mult1_
  // etc., not the user's copies) so the UI sees current effective values.
  std::vector<float> get_array(std::string_view name) const override {
    if (name == "mult1") return mult1.values.empty() ? mult1_ : mult1.values;
    if (name == "mult2") return mult2.values.empty() ? mult2_ : mult2.values;
    if (name == "ampl1") return ampl1.values.empty() ? ampl1_ : ampl1.values;
    if (name == "ampl2") return ampl2.values.empty() ? ampl2_ : ampl2.values;
    return {};
  }

protected:
  void init_arrays() override {
    if (mult1.values.empty()) {
      init_arrays_defaults();
      return;
    }
    mult1_ = mult1.values;
    mult2_ = mult2.values;
    ampl1_ = ampl1.values;
    ampl2_ = ampl2.values;

    int n = int(mult1_.size());
    po1_.resize(n);
    po2_.resize(n);
    for (int i = 0; i < n; ++i) {
      po1_[i] = std::fmod((mult1_[i] - 1.0f) * unitPO1.value, 1.0f);
      po2_[i] = std::fmod((mult2_[i] - 1.0f) * unitPO2.value, 1.0f);
    }
  }

private:
  void init_arrays_defaults() {
    int n = maxPartials.value;
    mult1_.resize(n); mult2_.resize(n);
    ampl1_.resize(n); ampl2_.resize(n);
    po1_.resize(n);   po2_.resize(n);
    for (int i = 0; i < n; ++i) {
      float m = float(i + 1);
      mult1_[i] = m; mult2_[i] = m;
      ampl1_[i] = 1.0f; ampl2_[i] = 1.0f;
      po1_[i] = 0.0f; po2_[i] = 0.0f;
    }
  }
};

// ---------------------------------------------------------------------------
// CompositePartials — combines multiple IPartials into one.
// Concatenates partial arrays from all children. Used to mix, e.g.,
// SequencePartials + ExplicitPartials in a single AdditiveSource.
// Ported from legacy CompositePartials.cs.
// ---------------------------------------------------------------------------
struct CompositePartials final : ValueSource, IPartials {

  const char* type_name() const override { return "CompositePartials"; }
  SourceCategory category() const override { return SourceCategory::Additive; }

  // The `partials` input is a list, held in `sets_`, not a pin: it is served
  // by name here, and wiring it appends (add_param routes here through the base).
  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"partials", true},  // multi-input: wire multiple Partials sources
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name != "partials") { ValueSource::set_param(name, std::move(src)); return; }
    auto* p = dynamic_cast<IPartials*>(src.get());
    if (p) sets_.push_back({std::move(src), p});
  }

  void clear_param(std::string_view name) override {
    if (name == "partials") sets_.clear();
    else ValueSource::clear_param(name);
  }

  // --- ValueSource interface (not an audio source itself) ---
  void on_prepare(const RenderContext& ctx, int frames) override { partials_prepare(ctx, frames); }
  float next() override { partials_next(); return 0.0f; }
  float current() const override { return 0.0f; }

  // --- IPartials interface ---
  void partials_prepare(const RenderContext& ctx, int frames) override {
    for (auto& e : sets_) e.ipartials->partials_prepare(ctx, frames);
  }

  void partials_next() override {
    for (auto& e : sets_) e.ipartials->partials_next();
  }

  int partial_count() const override {
    int total = 0;
    for (auto& e : sets_) total += e.ipartials->partial_count();
    return total;
  }

  float get_partial_value(float amplitude, float frequency, float phaseDiff,
                          int index, IFormant* formant, float formantWeight,
                          float formantFloor) override {
    int offset = 0;
    for (auto& e : sets_) {
      int count = e.ipartials->partial_count();
      if (index < offset + count)
        return e.ipartials->get_partial_value(amplitude, frequency, phaseDiff,
                                               index - offset, formant, formantWeight,
                                               formantFloor);
      offset += count;
    }
    return 0.0f;
  }

  // Walk each set once instead of re-scanning the set list per partial index
  // (the get_partial_value path above is O(sets) per partial). Order matches:
  // sets in declaration order, partials in index order within each set — and
  // threading `acc` through keeps the addition order identical to one flat
  // loop over the concatenated index space.
  float sum_partials(float acc, float amplitude, float frequency,
                     float phaseDiff, IFormant* formant, float fmtWt,
                     float fmtFloor) override {
    for (auto& e : sets_)
      acc = e.ipartials->sum_partials(acc, amplitude, frequency, phaseDiff,
                                      formant, fmtWt, fmtFloor);
    return acc;
  }

private:
  struct Entry {
    std::shared_ptr<ValueSource> source;  // prevents the shared_ptr from dying
    IPartials* ipartials;                  // fast pointer to the IPartials interface
  };
  std::vector<Entry> sets_;
};

} // namespace mforce
