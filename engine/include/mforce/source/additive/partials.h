#pragma once
// Partials: a PartialSet.
#include "mforce/source/additive/partial_set.h"
#include "mforce/source/additive/coherent_walk.h"
#include "mforce/source/additive/pair_trade.h"
#include "mforce/source/additive/bandwidth_noise.h"
#include "mforce/source/additive/onset_gate.h"
#include "mforce/source/additive/decay_law.h"
#include "mforce/source/additive/expand_rule.h"
#include "mforce/source/additive/partial_layout.h"
#include "mforce/source/additive/formant.h"
#include "mforce/core/fast_math.h"
#include "mforce/core/randomizer.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdint>

namespace mforce {

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
  kRngOnset         = 7,  // single (start-time only)
  kRngDetune        = 8,  // single (start-time only)
  kRngExpand        = 9,  // single (start-time only)
};

// ---------------------------------------------------------------------------
// Partials — abstract base class implementing the per-partial rendering
// engine. Ported faithfully from legacy Partials.cs GetPartialValue().
//
// Owns eight envelope pins, the rolloff/detune and layer settings, the expand
// rule, the layout (the partial arrays) and the six layers. Subclasses only
// implement init_arrays() to populate the layout's arrays.
// ---------------------------------------------------------------------------
struct Partials : PartialSet {
  // The base prepares, starts and advances the eight envelope pins in this
  // order, once per call; they are Manual because no sweep runs on a part.
  // All are unipolar 0-1 blend factors driving a lerp
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
  // inharmonicity — stiff-string stretch coefficient B: at note-on the
  // effective multiplier becomes m*sqrt(1 + B*m^2). Per-note B arrives as a
  // dynamic pin fed by a frequency curve. Default 0 = off (bit-exact).
  Setting<float> decayRate    {this, "decayRate",     0.0f,  0.0f, 400.0f};
  Setting<float> decayExp     {this, "decayExp",      0.0f, -2.0f, 4.0f};
  Setting<float> inharmonicity{this, "inharmonicity", 0.0f,  0.0f, 0.1f};

  Partials(uint32_t seed = 0xADD2'0000u) : baseSeed_(seed) {}

  const char* type_name() const override { return "Partials"; }

  void on_setting_changed(std::string_view name) override {
    if (name == "bandwidthHz" && bandwidthHz.value < 1.0f) bandwidthHz.value = 1.0f;
  }

  // --- Set by the patch loader from an "expandRule" block ---
  void set_expand_rule(ExpandRule rule) { expandRule_ = rule; hasExpand_ = true; }

  // --- The set's hooks: after the base has prepared, started or advanced the pins ---

  // The rate, and every array a note fills sized for it: once per build, idempotent.
  void on_prepare(const RenderContext& ctx) override {
    rate_ = float(ctx.sampleRate);
    prepare_note_arrays();
  }

  void on_start(int frames) override {
    // Per-note stream base: baseSeed_ hashed with a note counter. Every rng
    // stream below reseeds from this, so a note's randomness depends only on
    // (baseSeed, note ordinal, partial, layer) — never on how many draws any
    // earlier code path happened to make. The note counter preserves the old
    // per-note variety (the shared stream used to just continue across
    // note-ons); without it every note would replay identical noise.
    noteBase_ = uint32_t(splitmix64((uint64_t(baseSeed_) << 32) ^ uint64_t(noteSeq_)));
    ++noteSeq_;

    // Expand partials if rule is set
    if (hasExpand_) {
      // Back to the originals the last rebuild kept, then this note's expansion;
      // every assign fits the capacity prepare reserved.
      layout_.restore();
      expandRng_ = Randomizer(stream_seed(noteBase_, 0, kRngExpand));
      for (int r = 0; r <= expandRule_.recurse; ++r)
        layout_.expand(expandRule_, expandRng_);
    }

    // Every array below was sized in prepare (prepare_note_arrays): each assign
    // and resize here fills within that capacity and allocates nothing.
    int n = int(layout_.mult1.size());
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

    // Per-partial decay: the running gains back to unity at note-on; the
    // per-sample factors are filled by ensure_partial_cache alongside
    // pmultCache_ (they depend on the effective, possibly inharmonicity-
    // stretched, multiplier). Sized at prepare; render never allocates.
    decayActive_ = (decayRate.value != 0.0f);
    if (decayActive_) decay_.start(n);

    // Per-partial bandwidth-noise state. Each partial gets an INDEPENDENT
    // smoothed random walk (decorrelated start phase + targets) so the bands
    // fill in rather than wobbling in unison. Segment length = rate/bandwidthHz.
    bandwidth_.start(n, noteBase_, kRngBandwidth, bandwidthHz.value, rate_);

    // Motion / shimmer / trade / onset state. All per-note (start fires at
    // every note-on) and inert unless configured; every layer was sized at
    // prepare, so nothing here or in the per-sample path allocates.
    onset_.reset();
    motionActive_ = (motionDepth1.value != 0.0f || motionDepth2.value != 0.0f);
    if (motionActive_)
      motion_.start(n, noteBase_, kRngMotionShared, kRngMotionInd,
                    motionHz.value, motionEvolve.value, rate_);
    shimmerActive_ = (shimmerDepth1.value != 0.0f || shimmerDepth2.value != 0.0f);
    if (shimmerActive_)
      shimmer_.start(n, noteBase_, kRngShimmerShared, kRngShimmerInd,
                     shimmerHz.value, shimmerEvolve.value, rate_);
    tradeActive_ = (tradeDepth.value != 0.0f);
    if (tradeActive_) trade_.start(n, noteBase_, kRngTrade, tradeHz.value, rate_);
    onsetActive_ = (onsetSpread.value > 0.0f);
    if (onsetActive_)
      onset_.start(n, noteBase_, kRngOnset, onsetSpread.value, onsetTilt.value,
                   onsetFade.value, rate_);

    init_detune_values();

    // Seed the per-sample scalars so a get_partial_value() before the first
    // next() (e.g. a UI probe) never reads stale values.
    refresh_sample_scalars();
  }

  void on_next() override {
    refresh_sample_scalars();

    onset_.advance();
    if (motionActive_) motion_.advance(motionEvolve.value, motionCoherence.value);
    if (shimmerActive_) shimmer_.advance(shimmerEvolve.value, shimmerCoherence.value);
    if (tradeActive_) trade_.advance();
    if (decayActive_) {
      // Advance each partial's running decay gain by its per-sample factor.
      // ensure_partial_cache first: the decay's factors are derived there from
      // the effective multiplier (cheap no-op once the cache is valid).
      ensure_partial_cache();
      decay_.advance();
    }
  }

  int partial_count() const override {
    return int(layout_.mult1.size());
  }

  // Port of legacy Partials.cs GetPartialValue — the authoritative per-partial
  // math. Kept as the single-partial entry point; the
  // render path goes through sum_partials() below, which hoists everything
  // that is invariant across partials out of the loop.
  float get_partial_value(float amplitude, float frequency, float phaseDiff,
                          int index, Formant* formant, float fmtWt,
                          float fmtFloor) override
  {
    ensure_partial_cache();
    return partial_value_impl(amplitude, frequency, phaseDiff, index,
                              formant, fmtWt, fmtFloor);
  }

  // Batched per-sample sum. Same math, same accumulation order as the loop
  // the additive source used to run — renders are byte-identical.
  float sum_partials(float acc, float amplitude, float frequency,
                     float phaseDiff, Formant* formant, float fmtWt,
                     float fmtFloor) override
  {
    ensure_partial_cache();
    const int n = int(layout_.mult1.size());
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
                                  int index, Formant* formant, float fmtWt,
                                  float fmtFloor)
  {
    // Multiplier (can evolve between mult1 and mult2) — cached on multE.
    float pmult = pmultCache_[index];

    // Phase offset (can evolve)
    float ppo = layout_.po1[index] + (layout_.po2[index] - layout_.po1[index]) * sPoE_;

    // Frequency = multiplier * base freq * (1 + detune)
    float dt = sDt_ * dtVals_[index];
    float pfreq = pmult * frequency * (1.0f + dt);

    // Frequency motion: cents offset from the coherence-mixed random walk.
    // Coherent component moves all partials by the same cents (proportional
    // Hz — fuses like vibrato); independent component broadens lines.
    if (motionActive_) {
      if (sMd_ != 0.0f) {
        float cents = sMd_ * motion_.value(index);
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

    // Formant factor — additive boost with an out-of-band floor:
    //   factor = fmtFloor + fmtWt * gain-at-frequency
    // where a formant's gain is 0 outside every band it shapes, so out of
    // band the factor is fmtFloor.
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
    if (formant) fmtFactor = fmtFloor + fmtWt * formant->gain(pfreq);

    // Fade out partials near cutoff (within 1000 Hz) — legacy line 216
    float fade = (pfreq < CUTOFF - 1000.0f) ? 1.0f : (CUTOFF - pfreq) / 1000.0f;

    float pampl = amplitude *
        (layout_.ampl1[index] + (layout_.ampl2[index] - layout_.ampl1[index]) * sAmplE_) *
        rolloff * fmtFactor * fade;

    // Per-partial exponential decay (piano): one multiply by the running
    // gain advanced in next(). Inert at decayRate == 0.
    if (decayActive_) pampl *= decay_.gain(index);

    // Bandwidth enhancement (Loris / SMS): trade part of this partial's
    // sinusoidal energy for a noise band by amplitude-modulating it with an
    // independent per-partial smoothed noise. Energy-preserving mix:
    //   amp *= sqrt(1-bw) + sqrt(bw)*noise   (bw=0 → pure sine; bw=1 → full band)
    // bandwidthHz sets how fast the noise wiggles ≈ the band width. bwEnv lets
    // bandwidth ramp (e.g. high at the attack, settling — a coupled noisy attack).
    if (sBwActive_) pampl *= bandwidth_.factor(index, sBwDry_, sBwWet_);   // sqrt(1-bw), sqrt(bw): per-sample

    // Amplitude shimmer: slow per-partial gain wander (coherence-mixed walk).
    // shimmerFloor clamps the gain from below so high depths can't visit
    // near-silence (measured 14.6 dB mid-note dips at depth ~1.09); the
    // default 0 reproduces the old max(0, g) exactly.
    if (shimmerActive_) {
      float g = 1.0f + sSd_ * shimmer_.value(index);
      pampl *= (g < shimmerFloor.value ? shimmerFloor.value : g);
    }

    // Energy trading: adjacent pairs share one walk with opposite signs, so
    // energy sloshes between neighbors while the pair sum stays ~constant.
    if (tradeActive_) {
      float g = 1.0f + ((index & 1) ? -tradeDepth.value : tradeDepth.value) * trade_.value(index >> 1);
      pampl *= (g < 0.0f ? 0.0f : g);
    }

    // Onset dispersion: partial stays silent until its per-note delay, then
    // fades in. Phase keeps advancing while gated, so partials "enter" as
    // already-running oscillators rather than all striking at t=0.
    if (onsetActive_) pampl = onset_.apply(index, pampl);

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
    const int n = int(layout_.mult1.size());
    const float ro = rolloff1.value + (rolloff2.value - rolloff1.value) * sRoE_;
    const bool scale = (motionScale.value != 0.0f);
    const bool inharm = (inharmonicity.value != 0.0f);
    for (int i = 0; i < n; ++i) {
      float pmult = layout_.mult1[i] + (layout_.mult2[i] - layout_.mult1[i]) * sMultE_;
      // Stiff-string stretch: m -> m*sqrt(1 + B*m^2). Applied to the
      // effective multiplier so frequency, rolloff and the decay law all see
      // the stretched partial. Branch keeps B == 0 bit-exact.
      if (inharm) pmult *= std::sqrt(1.0f + inharmonicity.value * pmult * pmult);
      pmultCache_[i]   = pmult;
      rolloffCache_[i] = (ro == 0.0f) ? 1.0f : (1.0f / std::pow(pmult, ro));
      if (scale) moScaleCache_[i] = std::pow(pmult, motionScale.value);
      if (decayActive_) decay_.set_gain(i, pmult, decayRate.value, decayExp.value, rate_);
    }
    cachedMultE_ = sMultE_;
    cachedRoE_   = sRoE_;
    partialCacheValid_ = true;
  }

public:
  // Read-only access to the live partial arrays. Subclasses (FullPartials,
  // SequencePartials) populate these via init_arrays(). ExplicitPartials
  // overrides to prefer the user's Array values. Used by the UI's
  // partials strip view to render the bar chart.
  //
  // Triggers an array rebuild if a config change marked them dirty — without
  // this, configs set after construction (e.g. evenWeight2=0 from JSON load)
  // wouldn't take effect until the first render call start().
  std::vector<float> get_array(std::string_view name) const override {
    if (arrayUpdateReq_) const_cast<Partials*>(this)->update_arrays();
    if (name == "mult1") return layout_.mult1;
    if (name == "mult2") return layout_.mult2;
    if (name == "ampl1") return layout_.ampl1;
    if (name == "ampl2") return layout_.ampl2;
    return {};
  }

protected:
  // Subclass populates layout_.mult1, layout_.mult2, layout_.ampl1, layout_.ampl2, layout_.po1, layout_.po2
  virtual void init_arrays() {}

  void update_arrays() {
    init_arrays();
    snapshot_originals();
    arrayUpdateReq_ = false;
  }
  // The originals the expand rule starts every note from: the arrays as the
  // last rebuild made them, before any expansion. Every rebuild — update_arrays
  // here, a subclass's own rebuild on an edit — ends with this, so a note
  // never starts from arrays an earlier note expanded.
  void snapshot_originals() { layout_.snapshot(); }

  void init_detune_values() {
    Randomizer dtRng(stream_seed(noteBase_, 0, kRngDetune));
    int n = int(layout_.mult1.size());
    dtVals_.resize(n);
    for (int i = 0; i < n; ++i) {
      // Legacy: sign < 0 -> range(-0.5, 0), else -> range(0, 1)
      dtVals_[i] = (dtRng.sign() < 0) ? dtRng.range(-0.5f, 0.0f) : dtRng.range(0.0f, 1.0f);
    }
  }

  // Every array a note fills, sized once here for the count prepare can see
  // — the partial arrays as update_arrays made them, expanded by the rule
  // if there is one — so start fills within capacity and allocates
  // nothing; the rebuild on a changed setting happens here too. Idempotent:
  // the count comes from the originals, never from arrays a note expanded,
  // so a second prepare after a note changes nothing a render can see.
  void prepare_note_arrays() {
    if (arrayUpdateReq_) update_arrays();
    int n = hasExpand_ ? layout_.reserve_expanded(expandRule_) : layout_.count();
    for (auto* v : {&partialPos_, &partialPO_, &partialLPO_, &dtVals_, &pmultCache_,
                    &rolloffCache_, &moScaleCache_})
      v->assign(size_t(n), 0.0f);
    bandwidth_.prepare(n);
    onset_.prepare(n);
    decay_.prepare(n);
    motion_.prepare(n);
    shimmer_.prepare(n);
    trade_.prepare(n);
  }

  // Rng streams — one per (partial, layer) for the layers with per-partial
  // state, one per layer otherwise. All reseeded per note in start
  // from stream_seed(noteBase_, index, layerId); nothing in the render path
  // shares a stream. baseSeed_ comes from the node's JSON "seed".
  uint32_t baseSeed_;
  uint32_t noteSeq_{0};   // note-on ordinal, hashed into noteBase_
  uint32_t noteBase_{0};
  Randomizer expandRng_{0u};

  // The layers: each sized at prepare and started per note, inert unless
  // its flag is on
  bool motionActive_{false}, shimmerActive_{false};
  bool tradeActive_{false}, onsetActive_{false};
  bool decayActive_{false};
  CoherentWalk motion_, shimmer_;
  PairTrade trade_;
  BandwidthNoise bandwidth_;
  OnsetGate onset_;
  DecayLaw decay_;

  // The partial arrays, their originals and the expansion (init_arrays in a
  // subclass writes the arrays)
  PartialLayout layout_;

  // Per-partial runtime state
  std::vector<float> partialPos_, partialPO_, partialLPO_, dtVals_;

  // Per-sample scalars, refreshed once per next() (see
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

  bool arrayUpdateReq_{true};

  // Sample rate captured from ctx at prepare(); drives partial phase
  // advance in get_partial_value(). Derived from the context, not a second copy of it.
  float rate_{48000.0f};

  static constexpr float CUTOFF = 16000.0f;
};

} // namespace mforce
