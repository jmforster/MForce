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
  ExpandRuleNode()
  : spacing1_(std::make_shared<ConstantSource>(0.5f))
  , spacing2_(std::make_shared<ConstantSource>(0.5f))
  , dt1_(std::make_shared<ConstantSource>(0.01f))
  , dt2_(std::make_shared<ConstantSource>(0.01f))
  , loPct1_(std::make_shared<ConstantSource>(0.1f))
  , loPct2_(std::make_shared<ConstantSource>(0.1f))
  , power1_(std::make_shared<ConstantSource>(1.0f))
  , power2_(std::make_shared<ConstantSource>(1.0f))
  , po1_(std::make_shared<ConstantSource>(0.0f))
  , po2_(std::make_shared<ConstantSource>(0.0f)) {}

  int count{2};
  int recurse{0};

  // Field accessors for set_param / get_param wiring.
  void set_spacing1(std::shared_ptr<ValueSource> s) { spacing1_ = std::move(s); }
  void set_spacing2(std::shared_ptr<ValueSource> s) { spacing2_ = std::move(s); }
  void set_dt1(std::shared_ptr<ValueSource> s)      { dt1_      = std::move(s); }
  void set_dt2(std::shared_ptr<ValueSource> s)      { dt2_      = std::move(s); }
  void set_loPct1(std::shared_ptr<ValueSource> s)   { loPct1_   = std::move(s); }
  void set_loPct2(std::shared_ptr<ValueSource> s)   { loPct2_   = std::move(s); }
  void set_power1(std::shared_ptr<ValueSource> s)   { power1_   = std::move(s); }
  void set_power2(std::shared_ptr<ValueSource> s)   { power2_   = std::move(s); }
  void set_po1(std::shared_ptr<ValueSource> s)      { po1_      = std::move(s); }
  void set_po2(std::shared_ptr<ValueSource> s)      { po2_      = std::move(s); }

  // Snapshot to a plain ExpandRule for consumption by Partials. Reads the
  // current() of each field — at load time these are ConstantSource values,
  // at prepare time they could be the current value of a wired envelope.
  ExpandRule to_struct() const {
    ExpandRule r;
    r.count = count;
    r.recurse = recurse;
    r.spacing1 = spacing1_->current();
    r.spacing2 = spacing2_->current();
    r.dt1 = dt1_->current();
    r.dt2 = dt2_->current();
    r.loPct1 = loPct1_->current();
    r.loPct2 = loPct2_->current();
    r.power1 = power1_->current();
    r.power2 = power2_->current();
    r.po1 = po1_->current();
    r.po2 = po2_->current();
    return r;
  }

  const char* type_name() const override { return "ExpandRule"; }
  SourceCategory category() const override { return SourceCategory::Additive; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"spacing1", 0.5f,  0.0f, 24.0f, "semis"},
      {"spacing2", 0.5f,  0.0f, 24.0f, "semis"},
      {"dt1",      0.01f, 0.0f, 1.0f,  "0-1"},
      {"dt2",      0.01f, 0.0f, 1.0f,  "0-1"},
      {"loPct1",   0.1f,  0.0f, 1.0f,  "0-1"},
      {"loPct2",   0.1f,  0.0f, 1.0f,  "0-1"},
      {"power1",   1.0f,  0.0f, 10.0f, ""},
      {"power2",   1.0f,  0.0f, 10.0f, ""},
      {"po1",      0.0f,  0.0f, 1.0f,  "cycles"},
      {"po2",      0.0f,  0.0f, 1.0f,  "cycles"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "spacing1") { set_spacing1(std::move(src)); return; }
    if (name == "spacing2") { set_spacing2(std::move(src)); return; }
    if (name == "dt1")      { set_dt1(std::move(src));      return; }
    if (name == "dt2")      { set_dt2(std::move(src));      return; }
    if (name == "loPct1")   { set_loPct1(std::move(src));   return; }
    if (name == "loPct2")   { set_loPct2(std::move(src));   return; }
    if (name == "power1")   { set_power1(std::move(src));   return; }
    if (name == "power2")   { set_power2(std::move(src));   return; }
    if (name == "po1")      { set_po1(std::move(src));      return; }
    if (name == "po2")      { set_po2(std::move(src));      return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "spacing1") return spacing1_;
    if (name == "spacing2") return spacing2_;
    if (name == "dt1")      return dt1_;
    if (name == "dt2")      return dt2_;
    if (name == "loPct1")   return loPct1_;
    if (name == "loPct2")   return loPct2_;
    if (name == "power1")   return power1_;
    if (name == "power2")   return power2_;
    if (name == "po1")      return po1_;
    if (name == "po2")      return po2_;
    return nullptr;
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"count",   ConfigType::Int, 2.0f, 0.0f, 16.0f},
      {"recurse", ConfigType::Int, 0.0f, 0.0f, 4.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "count")   { count = int(value);   return; }
    if (name == "recurse") { recurse = int(value); return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "count")   return float(count);
    if (name == "recurse") return float(recurse);
    return 0.0f;
  }

  void prepare(const RenderContext& /*ctx*/, int /*frames*/) override {}
  float next() override { return 0.0f; }
  float current() const override { return 0.0f; }

private:
  std::shared_ptr<ValueSource> spacing1_, spacing2_;
  std::shared_ptr<ValueSource> dt1_, dt2_;
  std::shared_ptr<ValueSource> loPct1_, loPct2_;
  std::shared_ptr<ValueSource> power1_, power2_;
  std::shared_ptr<ValueSource> po1_, po2_;
};

// Motion-layer config descriptor entries shared by Partials and its
// subclasses. Subclasses override config_descriptors() wholesale (each list
// re-declares the base entries — established pattern), so the motion block
// is spliced into every list via this macro to keep them in sync.
#define MFORCE_PARTIALS_MOTION_CONFIG_DESCS \
      {"motionDepth1",     ConfigType::Float, 0.0f,  0.0f,  400.0f}, \
      {"motionDepth2",     ConfigType::Float, 0.0f,  0.0f,  400.0f}, \
      {"motionHz",         ConfigType::Float, 4.0f,  0.01f, 200.0f}, \
      {"motionCoherence",  ConfigType::Float, 1.0f,  0.0f,  1.0f},   \
      {"motionEvolve",     ConfigType::Float, 0.0f,  0.0f,  1.0f},   \
      {"motionScale",      ConfigType::Float, 0.0f, -2.0f,  2.0f},   \
      {"shimmerDepth1",    ConfigType::Float, 0.0f,  0.0f,  1.0f},   \
      {"shimmerDepth2",    ConfigType::Float, 0.0f,  0.0f,  1.0f},   \
      {"shimmerHz",        ConfigType::Float, 3.0f,  0.01f, 200.0f}, \
      {"shimmerCoherence", ConfigType::Float, 0.0f,  0.0f,  1.0f},   \
      {"shimmerEvolve",    ConfigType::Float, 0.0f,  0.0f,  1.0f},   \
      {"tradeDepth",       ConfigType::Float, 0.0f,  0.0f,  1.0f},   \
      {"tradeHz",          ConfigType::Float, 2.0f,  0.01f, 50.0f},  \
      {"onsetSpread",      ConfigType::Float, 0.0f,  0.0f,  2.0f},   \
      {"onsetTilt",        ConfigType::Float, 0.0f, -1.0f,  1.0f},   \
      {"onsetFade",        ConfigType::Float, 0.03f, 0.001f, 1.0f},

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

// ---------------------------------------------------------------------------
// Partials — abstract base class implementing the per-partial rendering
// engine. Ported faithfully from legacy Partials.cs GetPartialValue().
//
// Owns 5 envelope inputs, rolloff/detune params, expand rule, and all
// static + runtime arrays. Subclasses only implement init_arrays() to
// populate the static arrays (mult1_, mult2_, ampl1_, ampl2_, po1_, po2_).
// ---------------------------------------------------------------------------
struct Partials : ValueSource, IPartials {
  Partials(uint32_t seed = 0xADD2'0000u)
  : rng_(seed)
  , multEnv_(std::make_shared<ConstantSource>(0.0f))
  , amplEnv_(std::make_shared<ConstantSource>(0.0f))
  , poEnv_(std::make_shared<ConstantSource>(0.0f))
  , roEnv_(std::make_shared<ConstantSource>(0.0f))
  , dtEnv_(std::make_shared<ConstantSource>(0.0f))
  , bwEnv_(std::make_shared<ConstantSource>(0.0f))
  , motionEnv_(std::make_shared<ConstantSource>(0.0f))
  , shimmerEnv_(std::make_shared<ConstantSource>(0.0f)) {}

  // --- ValueSource interface ---
  // Partials is a ValueSource for graph wiring, but doesn't produce audio.
  // Prepare/next delegate to partials_prepare/partials_next.
  void prepare(const RenderContext& ctx, int frames) override { partials_prepare(ctx, frames); }
  float next() override { partials_next(); return 0.0f; }
  float current() const override { return 0.0f; }

  const char* type_name() const override { return "Partials"; }
  SourceCategory category() const override { return SourceCategory::Additive; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    // All five "Env" params are unipolar 0-1 blend factors driving a lerp
    // between _1 and _2 endpoints (e.g. roEnv blends rolloff1 → rolloff2).
    // Wire an Envelope or RangeSource; a bipolar oscillator won't blend right.
    static constexpr ParamDescriptor descs[] = {
      {"multEnv", 0.0f, 0.0f, 1.0f, "0-1"},
      {"amplEnv", 0.0f, 0.0f, 1.0f, "0-1"},
      {"poEnv",   0.0f, 0.0f, 1.0f, "0-1"},
      {"roEnv",   0.0f, 0.0f, 1.0f, "0-1"},
      {"dtEnv",   0.0f, 0.0f, 1.0f, "0-1"},
      {"bwEnv",   0.0f, 0.0f, 1.0f, "0-1"},  // blends bandwidth1 → bandwidth2
      {"motionEnv",  0.0f, 0.0f, 1.0f, "0-1"},  // blends motionDepth1 → motionDepth2
      {"shimmerEnv", 0.0f, 0.0f, 1.0f, "0-1"},  // blends shimmerDepth1 → shimmerDepth2
    };
    return descs;
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"rolloff1", ConfigType::Float, 1.0f, 0.0f, 10.0f},
      {"rolloff2", ConfigType::Float, 1.0f, 0.0f, 10.0f},
      {"detune1",  ConfigType::Float, 0.0f, 0.0f, 1.0f},
      {"detune2",  ConfigType::Float, 0.0f, 0.0f, 1.0f},
      {"bandwidth1",  ConfigType::Float, 0.0f, 0.0f, 1.0f},
      {"bandwidth2",  ConfigType::Float, 0.0f, 0.0f, 1.0f},
      {"bandwidthHz", ConfigType::Float, 30.0f, 1.0f, 2000.0f},
      MFORCE_PARTIALS_MOTION_CONFIG_DESCS
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "multEnv") { multEnv_ = std::move(src); return; }
    if (name == "amplEnv") { amplEnv_ = std::move(src); return; }
    if (name == "poEnv")   { poEnv_ = std::move(src); return; }
    if (name == "roEnv")   { roEnv_ = std::move(src); return; }
    if (name == "dtEnv")   { dtEnv_ = std::move(src); return; }
    if (name == "bwEnv")   { bwEnv_ = std::move(src); return; }
    if (name == "motionEnv")  { motionEnv_ = std::move(src); return; }
    if (name == "shimmerEnv") { shimmerEnv_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "multEnv") return multEnv_;
    if (name == "amplEnv") return amplEnv_;
    if (name == "poEnv")   return poEnv_;
    if (name == "roEnv")   return roEnv_;
    if (name == "dtEnv")   return dtEnv_;
    if (name == "bwEnv")   return bwEnv_;
    if (name == "motionEnv")  return motionEnv_;
    if (name == "shimmerEnv") return shimmerEnv_;
    return nullptr;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "rolloff1")   { ro1_ = value; return; }
    if (name == "rolloff2")   { ro2_ = value; return; }
    if (name == "detune1")    { dt1_ = value; return; }
    if (name == "detune2")    { dt2_ = value; return; }
    if (name == "bandwidth1")  { bw1_ = value; return; }
    if (name == "bandwidth2")  { bw2_ = value; return; }
    if (name == "bandwidthHz") { bwHz_ = (value < 1.0f ? 1.0f : value); return; }
    if (name == "motionDepth1")     { moDepth1_ = value; return; }
    if (name == "motionDepth2")     { moDepth2_ = value; return; }
    if (name == "motionHz")         { moHz_ = value; return; }
    if (name == "motionCoherence")  { moCoherence_ = value; return; }
    if (name == "motionEvolve")     { moEvolve_ = value; return; }
    if (name == "motionScale")      { moScale_ = value; return; }
    if (name == "shimmerDepth1")    { shDepth1_ = value; return; }
    if (name == "shimmerDepth2")    { shDepth2_ = value; return; }
    if (name == "shimmerHz")        { shHz_ = value; return; }
    if (name == "shimmerCoherence") { shCoherence_ = value; return; }
    if (name == "shimmerEvolve")    { shEvolve_ = value; return; }
    if (name == "tradeDepth")       { trDepth_ = value; return; }
    if (name == "tradeHz")          { trHz_ = value; return; }
    if (name == "onsetSpread")      { onsetSpread_ = value; return; }
    if (name == "onsetTilt")        { onsetTilt_ = value; return; }
    if (name == "onsetFade")        { onsetFade_ = value; return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "rolloff1")   return ro1_;
    if (name == "rolloff2")   return ro2_;
    if (name == "detune1")    return dt1_;
    if (name == "detune2")    return dt2_;
    if (name == "bandwidth1")  return bw1_;
    if (name == "bandwidth2")  return bw2_;
    if (name == "bandwidthHz") return bwHz_;
    if (name == "motionDepth1")     return moDepth1_;
    if (name == "motionDepth2")     return moDepth2_;
    if (name == "motionHz")         return moHz_;
    if (name == "motionCoherence")  return moCoherence_;
    if (name == "motionEvolve")     return moEvolve_;
    if (name == "motionScale")      return moScale_;
    if (name == "shimmerDepth1")    return shDepth1_;
    if (name == "shimmerDepth2")    return shDepth2_;
    if (name == "shimmerHz")        return shHz_;
    if (name == "shimmerCoherence") return shCoherence_;
    if (name == "shimmerEvolve")    return shEvolve_;
    if (name == "tradeDepth")       return trDepth_;
    if (name == "tradeHz")          return trHz_;
    if (name == "onsetSpread")      return onsetSpread_;
    if (name == "onsetTilt")        return onsetTilt_;
    if (name == "onsetFade")        return onsetFade_;
    return 0.0f;
  }

  // --- Array accessors (for AdditiveSource2 compat) ---
  const std::vector<float>& get_mult1() const { return mult1_; }
  const std::vector<float>& get_mult2() const { return mult2_; }
  const std::vector<float>& get_ampl1() const { return ampl1_; }
  const std::vector<float>& get_ampl2() const { return ampl2_; }

  // --- Setters for backward compat / programmatic use ---
  void set_ro(float ro1, float ro2) { ro1_ = ro1; ro2_ = ro2; }
  void set_dt(float dt1, float dt2) { dt1_ = dt1; dt2_ = dt2; }
  void set_expand_rule(ExpandRule rule) { expandRule_ = rule; hasExpand_ = true; }

  // --- IPartials interface ---

  void partials_prepare(const RenderContext& ctx, int frames) override {
    rate_ = float(ctx.sampleRate);

    if (arrayUpdateReq_) {
      update_arrays();
    }

    multEnv_->prepare(ctx, frames);
    amplEnv_->prepare(ctx, frames);
    poEnv_->prepare(ctx, frames);
    roEnv_->prepare(ctx, frames);
    dtEnv_->prepare(ctx, frames);
    bwEnv_->prepare(ctx, frames);
    motionEnv_->prepare(ctx, frames);
    shimmerEnv_->prepare(ctx, frames);

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

    // Per-partial bandwidth-noise state. Each partial gets an INDEPENDENT
    // smoothed random walk (decorrelated start phase + targets) so the bands
    // fill in rather than wobbling in unison. Segment length = rate/bandwidthHz.
    bwLen_ = std::max(1, int(rate_ / (bwHz_ < 1.0f ? 1.0f : bwHz_)));
    bwCur_.assign(n, 0.0f);
    bwTarget_.assign(n, 0.0f);
    bwPos_.assign(n, 0);
    for (int i = 0; i < n; ++i) {
      bwCur_[i]    = rng_.valuePN();
      bwTarget_[i] = rng_.valuePN();
      bwPos_[i]    = int(rng_.range(0.0f, float(bwLen_)));  // stagger phase
    }

    // Motion / shimmer / trade / onset state. All per-note (prepare fires at
    // every note-on) and inert unless configured. Allocation happens here,
    // never in the per-sample path.
    sampleIdx_ = 0;
    motionActive_ = (moDepth1_ != 0.0f || moDepth2_ != 0.0f);
    if (motionActive_) {
      moLen_ = std::max(1, int(rate_ / std::max(0.01f, moHz_)));
      walk_init(moShared_, moLen_, moEvolve_);
      moWalks_.assign(n, MotionWalk{});
      for (auto& w : moWalks_) walk_init(w, moLen_, moEvolve_);
      moVals_.assign(n, 0.0f);
    }
    shimmerActive_ = (shDepth1_ != 0.0f || shDepth2_ != 0.0f);
    if (shimmerActive_) {
      shLen_ = std::max(1, int(rate_ / std::max(0.01f, shHz_)));
      walk_init(shShared_, shLen_, shEvolve_);
      shWalks_.assign(n, MotionWalk{});
      for (auto& w : shWalks_) walk_init(w, shLen_, shEvolve_);
      shVals_.assign(n, 0.0f);
    }
    tradeActive_ = (trDepth_ != 0.0f);
    if (tradeActive_) {
      trLen_ = std::max(1, int(rate_ / std::max(0.01f, trHz_)));
      trWalks_.assign((n + 1) / 2, MotionWalk{});
      for (auto& w : trWalks_) walk_init(w, trLen_, 0.0f);
      trVals_.assign((n + 1) / 2, 0.0f);
    }
    onsetActive_ = (onsetSpread_ > 0.0f);
    if (onsetActive_) {
      // Per-partial onset delay: tilt orders delays by partial height
      // (+1 low-first / high partials bloom later, -1 reverse, 0 random).
      onsetDelay_.resize(n);
      float at = std::fabs(onsetTilt_);
      for (int i = 0; i < n; ++i) {
        float u = rng_.value();
        float r = (n > 1) ? float(i) / float(n - 1) : 0.0f;
        float ordered = (onsetTilt_ >= 0.0f) ? r : 1.0f - r;
        onsetDelay_[i] = ((1.0f - at) * u + at * ordered) * onsetSpread_ * rate_;
      }
      onsetFadeSamples_ = std::max(1.0f, onsetFade_ * rate_);
    }

    init_detune_values();

    // Seed the per-sample scalars so a get_partial_value() before the first
    // partials_next() (e.g. a UI probe) never reads stale values.
    refresh_sample_scalars();
  }

  void partials_next() override {
    multEnv_->next();
    amplEnv_->next();
    poEnv_->next();
    roEnv_->next();
    dtEnv_->next();
    bwEnv_->next();
    motionEnv_->next();
    shimmerEnv_->next();

    refresh_sample_scalars();

    ++sampleIdx_;
    if (motionActive_) {
      float shared = walk_advance(moShared_, moLen_, moEvolve_);
      float coh = moCoherence_;
      for (size_t i = 0; i < moWalks_.size(); ++i) {
        float ind = walk_advance(moWalks_[i], moLen_, moEvolve_);
        moVals_[i] = coh * shared + (1.0f - coh) * ind;
      }
    }
    if (shimmerActive_) {
      float shared = walk_advance(shShared_, shLen_, shEvolve_);
      float coh = shCoherence_;
      for (size_t i = 0; i < shWalks_.size(); ++i) {
        float ind = walk_advance(shWalks_[i], shLen_, shEvolve_);
        shVals_[i] = coh * shared + (1.0f - coh) * ind;
      }
    }
    if (tradeActive_) {
      for (size_t i = 0; i < trWalks_.size(); ++i)
        trVals_[i] = walk_advance(trWalks_[i], trLen_, 0.0f);
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
        if (moScale_ != 0.0f) cents *= moScaleCache_[index];
        // fast_exp2, not std::exp2: this is a CRT call executed once per
        // partial per sample, and it measured at 65.6% of viola_default's
        // entire loop cost (tools/ablate_layers.py). The replacement is
        // accurate to 0.88 float32 eps — see core/fast_math.h.
        pfreq *= fast_exp2(cents * (1.0f / 1200.0f));
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

    // Bandwidth enhancement (Loris / SMS): trade part of this partial's
    // sinusoidal energy for a noise band by amplitude-modulating it with an
    // independent per-partial smoothed noise. Energy-preserving mix:
    //   amp *= sqrt(1-bw) + sqrt(bw)*noise   (bw=0 → pure sine; bw=1 → full band)
    // bandwidthHz sets how fast the noise wiggles ≈ the band width. bwEnv lets
    // bandwidth ramp (e.g. high at the attack, settling — a coupled noisy attack).
    if (sBwActive_) {
      if (bwPos_[index] >= bwLen_) {
        bwCur_[index]    = bwTarget_[index];
        bwTarget_[index] = rng_.valuePN();
        bwPos_[index]    = 0;
      }
      float u = float(bwPos_[index]) / float(bwLen_);
      float s = u * u * (3.0f - 2.0f * u);  // smoothstep → low-pass noise
      float noise = bwCur_[index] + (bwTarget_[index] - bwCur_[index]) * s;
      ++bwPos_[index];
      pampl *= sBwDry_ + sBwWet_ * noise;   // sqrt(1-bw), sqrt(bw): per-sample
    }

    // Amplitude shimmer: slow per-partial gain wander (coherence-mixed walk).
    if (shimmerActive_) {
      float g = 1.0f + sSd_ * shVals_[index];
      pampl *= (g < 0.0f ? 0.0f : g);
    }

    // Energy trading: adjacent pairs share one walk with opposite signs, so
    // energy sloshes between neighbors while the pair sum stays ~constant.
    if (tradeActive_) {
      float g = 1.0f + ((index & 1) ? -trDepth_ : trDepth_) * trVals_[index >> 1];
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
    sMultE_ = multEnv_->current();
    sAmplE_ = amplEnv_->current();
    sPoE_   = poEnv_->current();
    sRoE_   = roEnv_->current();
    sDtE_   = dtEnv_->current();

    sDt_ = dt1_ + (dt2_ - dt1_) * sDtE_;
    sMd_ = moDepth1_ + (moDepth2_ - moDepth1_) * motionEnv_->current();
    sSd_ = shDepth1_ + (shDepth2_ - shDepth1_) * shimmerEnv_->current();

    float bw = bw1_ + (bw2_ - bw1_) * bwEnv_->current();
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
    const float ro = ro1_ + (ro2_ - ro1_) * sRoE_;
    const bool scale = (moScale_ != 0.0f);
    for (int i = 0; i < n; ++i) {
      float pmult = mult1_[i] + (mult2_[i] - mult1_[i]) * sMultE_;
      pmultCache_[i]   = pmult;
      rolloffCache_[i] = (ro == 0.0f) ? 1.0f : (1.0f / std::pow(pmult, ro));
      if (scale) moScaleCache_[i] = std::pow(pmult, moScale_);
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
    int n = int(mult1_.size());
    dtVals_.resize(n);
    for (int i = 0; i < n; ++i) {
      // Legacy: sign < 0 -> range(-0.5, 0), else -> range(0, 1)
      dtVals_[i] = (rng_.sign() < 0) ? rng_.range(-0.5f, 0.0f) : rng_.range(0.0f, 1.0f);
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

        m1[idx] = mult1_[i] / std::pow(2.0f, semis1 / 12.0f) * (1.0f + rng_.valuePN() * er.dt1);
        m2[idx] = mult2_[i] / std::pow(2.0f, semis2 / 12.0f) * (1.0f + rng_.valuePN() * er.dt2);

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

  int walk_seg_len(int baseLen, float evolve) {
    float f = (evolve <= 0.0f) ? 1.0f
                               : std::pow(1.0f + 3.0f * evolve, rng_.valuePN());
    int len = int(float(baseLen) * f);
    return len < 1 ? 1 : len;
  }

  void walk_init(MotionWalk& w, int baseLen, float evolve) {
    w.cur = rng_.valuePN();
    w.target = rng_.valuePN();
    w.len = walk_seg_len(baseLen, evolve);
    w.pos = int(rng_.range(0.0f, float(w.len)));  // stagger segment phase
  }

  float walk_advance(MotionWalk& w, int baseLen, float evolve) {
    if (w.pos >= w.len) {
      w.cur = w.target;
      float amp = 1.0f + 0.7f * evolve * rng_.valuePN();
      w.target = rng_.valuePN() * amp;
      w.len = walk_seg_len(baseLen, evolve);
      w.pos = 0;
    }
    float u = float(w.pos) / float(w.len);
    float s = u * u * (3.0f - 2.0f * u);  // smoothstep → low-pass movement
    ++w.pos;
    return w.cur + (w.target - w.cur) * s;
  }

  Randomizer rng_;

  // Envelopes
  std::shared_ptr<ValueSource> multEnv_;
  std::shared_ptr<ValueSource> amplEnv_;
  std::shared_ptr<ValueSource> poEnv_;
  std::shared_ptr<ValueSource> roEnv_;
  std::shared_ptr<ValueSource> dtEnv_;
  std::shared_ptr<ValueSource> bwEnv_;
  std::shared_ptr<ValueSource> motionEnv_;
  std::shared_ptr<ValueSource> shimmerEnv_;

  // Global rolloff/detune ranges
  float ro1_{1.0f}, ro2_{1.0f};
  float dt1_{0.0f}, dt2_{0.0f};

  // Bandwidth enhancement: bw1_→bw2_ mix amount (blended by bwEnv_), bwHz_ band
  // width. Default 0 → feature inert (no change to existing patches).
  float bw1_{0.0f}, bw2_{0.0f}, bwHz_{30.0f};
  int   bwLen_{1600};

  // Motion layer configs (all default-inert; see partial-motion design spec)
  float moDepth1_{0.0f}, moDepth2_{0.0f}, moHz_{4.0f};
  float moCoherence_{1.0f}, moEvolve_{0.0f}, moScale_{0.0f};
  float shDepth1_{0.0f}, shDepth2_{0.0f}, shHz_{3.0f};
  float shCoherence_{0.0f}, shEvolve_{0.0f};
  float trDepth_{0.0f}, trHz_{2.0f};
  float onsetSpread_{0.0f}, onsetTilt_{0.0f}, onsetFade_{0.03f};

  // Motion layer runtime state (allocated in partials_prepare)
  bool motionActive_{false}, shimmerActive_{false};
  bool tradeActive_{false}, onsetActive_{false};
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
  FullPartials(uint32_t seed = 0xADD2'0000u) : Partials(seed) { init_arrays(); }

  const char* type_name() const override { return "FullPartials"; }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"maxPartials",  ConfigType::Int,   30.0f, 1.0f, 200.0f},
      {"minMult",      ConfigType::Int,   1.0f,  1.0f, 100.0f},
      {"evenWeight1",  ConfigType::Float, 1.0f,  0.0f, 10.0f},
      {"evenWeight2",  ConfigType::Float, 1.0f,  0.0f, 10.0f},
      {"oddWeight1",   ConfigType::Float, 1.0f,  0.0f, 10.0f},
      {"oddWeight2",   ConfigType::Float, 1.0f,  0.0f, 10.0f},
      {"unitPO1",      ConfigType::Float, 0.0f,  0.0f, 1.0f},
      {"unitPO2",      ConfigType::Float, 0.0f,  0.0f, 1.0f},
      {"rolloff1",     ConfigType::Float, 1.0f,  0.0f, 10.0f},
      {"rolloff2",     ConfigType::Float, 1.0f,  0.0f, 10.0f},
      {"detune1",      ConfigType::Float, 0.0f,  0.0f, 1.0f},
      {"detune2",      ConfigType::Float, 0.0f,  0.0f, 1.0f},
      {"bandwidth1",   ConfigType::Float, 0.0f,  0.0f, 1.0f},
      {"bandwidth2",   ConfigType::Float, 0.0f,  0.0f, 1.0f},
      {"bandwidthHz",  ConfigType::Float, 30.0f, 1.0f, 2000.0f},
      MFORCE_PARTIALS_MOTION_CONFIG_DESCS
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "maxPartials")  { maxPartials_ = std::max(1, int(value)); arrayUpdateReq_ = true; return; }
    if (name == "minMult")      { minMult_ = std::max(1, int(value)); arrayUpdateReq_ = true; return; }
    if (name == "evenWeight1")  { evenWeight1_ = value; arrayUpdateReq_ = true; return; }
    if (name == "evenWeight2")  { evenWeight2_ = value; arrayUpdateReq_ = true; return; }
    if (name == "oddWeight1")   { oddWeight1_ = value; arrayUpdateReq_ = true; return; }
    if (name == "oddWeight2")   { oddWeight2_ = value; arrayUpdateReq_ = true; return; }
    if (name == "unitPO1")      { unitPO1_ = value; arrayUpdateReq_ = true; return; }
    if (name == "unitPO2")      { unitPO2_ = value; arrayUpdateReq_ = true; return; }
    Partials::set_config(name, value);
  }

  float get_config(std::string_view name) const override {
    if (name == "maxPartials")  return float(maxPartials_);
    if (name == "minMult")      return float(minMult_);
    if (name == "evenWeight1")  return evenWeight1_;
    if (name == "evenWeight2")  return evenWeight2_;
    if (name == "oddWeight1")   return oddWeight1_;
    if (name == "oddWeight2")   return oddWeight2_;
    if (name == "unitPO1")      return unitPO1_;
    if (name == "unitPO2")      return unitPO2_;
    return Partials::get_config(name);
  }

  // Programmatic setup (backward compat with old FullAdditiveSource::init_full_partials)
  void setup(int maxPartials, int minMult,
             float ew1, float ew2, float ow1, float ow2,
             float unitPO1, float unitPO2) {
    maxPartials_ = maxPartials; minMult_ = minMult;
    evenWeight1_ = ew1; evenWeight2_ = ew2;
    oddWeight1_ = ow1; oddWeight2_ = ow2;
    unitPO1_ = unitPO1; unitPO2_ = unitPO2;
    init_arrays();
  }

protected:
  void init_arrays() override {
    int n = maxPartials_;
    mult1_.resize(n); mult2_.resize(n);
    ampl1_.resize(n); ampl2_.resize(n);
    po1_.resize(n);   po2_.resize(n);

    for (int i = 0; i < n; ++i) {
      float m = float(minMult_ + i);
      mult1_[i] = m;

      // Even/odd amplitude weights (fundamental always 1) — legacy FullPartials.cs
      if (int(std::trunc(m)) % 2 == 0) {
        ampl1_[i] = evenWeight1_;
        ampl2_[i] = evenWeight2_;
      } else {
        ampl1_[i] = (m == 1.0f) ? 1.0f : oddWeight1_;
        ampl2_[i] = (m == 1.0f) ? 1.0f : oddWeight2_;
      }

      // Phase offsets: (mult - 1) * unitPO
      po1_[i] = std::fmod((m - 1.0f) * unitPO1_, 1.0f);
      po2_[i] = std::fmod((m - 1.0f) * unitPO2_, 1.0f);
    }

    // No multiplier evolution for FullPartials
    mult2_ = mult1_;
  }

private:
  int maxPartials_{30};
  int minMult_{1};
  float evenWeight1_{1.0f}, evenWeight2_{1.0f};
  float oddWeight1_{1.0f}, oddWeight2_{1.0f};
  float unitPO1_{0.0f}, unitPO2_{0.0f};
};

// ---------------------------------------------------------------------------
// SequencePartials — linear multiplier sequences with evolving spacing.
// Ported from legacy SequencePartials.cs InitArrays.
// ---------------------------------------------------------------------------
struct SequencePartials final : Partials {
  SequencePartials(uint32_t seed = 0xADD2'0000u) : Partials(seed) { init_arrays(); }

  const char* type_name() const override { return "SequencePartials"; }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"maxPartials", ConfigType::Int,   30.0f, 1.0f, 200.0f},
      {"minMult1",    ConfigType::Float, 1.0f,  0.01f, 100.0f},
      {"minMult2",    ConfigType::Float, 1.0f,  0.01f, 100.0f},
      {"incr1",       ConfigType::Float, 1.0f,  0.01f, 100.0f},
      {"incr2",       ConfigType::Float, 1.0f,  0.01f, 100.0f},
      {"unitPO1",     ConfigType::Float, 0.0f,  0.0f, 1.0f},
      {"unitPO2",     ConfigType::Float, 0.0f,  0.0f, 1.0f},
      {"rolloff1",    ConfigType::Float, 1.0f,  0.0f, 10.0f},
      {"rolloff2",    ConfigType::Float, 1.0f,  0.0f, 10.0f},
      {"detune1",     ConfigType::Float, 0.0f,  0.0f, 1.0f},
      {"detune2",     ConfigType::Float, 0.0f,  0.0f, 1.0f},
      {"bandwidth1",  ConfigType::Float, 0.0f,  0.0f, 1.0f},
      {"bandwidth2",  ConfigType::Float, 0.0f,  0.0f, 1.0f},
      {"bandwidthHz", ConfigType::Float, 30.0f, 1.0f, 2000.0f},
      MFORCE_PARTIALS_MOTION_CONFIG_DESCS
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "maxPartials") { maxPartials_ = std::max(1, int(value)); arrayUpdateReq_ = true; return; }
    if (name == "minMult1")    { minMult1_ = value; arrayUpdateReq_ = true; return; }
    if (name == "minMult2")    { minMult2_ = value; arrayUpdateReq_ = true; return; }
    if (name == "incr1")       { incr1_ = value; arrayUpdateReq_ = true; return; }
    if (name == "incr2")       { incr2_ = value; arrayUpdateReq_ = true; return; }
    if (name == "unitPO1")     { unitPO1_ = value; arrayUpdateReq_ = true; return; }
    if (name == "unitPO2")     { unitPO2_ = value; arrayUpdateReq_ = true; return; }
    Partials::set_config(name, value);
  }

  float get_config(std::string_view name) const override {
    if (name == "maxPartials") return float(maxPartials_);
    if (name == "minMult1")    return minMult1_;
    if (name == "minMult2")    return minMult2_;
    if (name == "incr1")       return incr1_;
    if (name == "incr2")       return incr2_;
    if (name == "unitPO1")     return unitPO1_;
    if (name == "unitPO2")     return unitPO2_;
    return Partials::get_config(name);
  }

  // Programmatic setup
  void setup(int maxPartials, float minMult1, float minMult2,
             float incr1, float incr2, float unitPO1, float unitPO2) {
    maxPartials_ = maxPartials;
    minMult1_ = minMult1; minMult2_ = minMult2;
    incr1_ = incr1; incr2_ = incr2;
    unitPO1_ = unitPO1; unitPO2_ = unitPO2;
    init_arrays();
  }

protected:
  void init_arrays() override {
    int n = maxPartials_;
    mult1_.resize(n); mult2_.resize(n);
    ampl1_.assign(n, 1.0f); ampl2_.assign(n, 1.0f);
    po1_.resize(n); po2_.resize(n);

    for (int i = 0; i < n; ++i) {
      mult1_[i] = minMult1_ + float(i) * incr1_;
      mult2_[i] = minMult2_ + float(i) * incr2_;
      po1_[i] = std::fmod((mult1_[i] - 1.0f) * unitPO1_, 1.0f);
      po2_[i] = std::fmod((mult2_[i] - 1.0f) * unitPO2_, 1.0f);
    }
  }

private:
  int maxPartials_{30};
  float minMult1_{1.0f}, minMult2_{1.0f};
  float incr1_{1.0f}, incr2_{1.0f};
  float unitPO1_{0.0f}, unitPO2_{0.0f};
};

// ---------------------------------------------------------------------------
// ExplicitPartials — user-specified multiplier and amplitude arrays.
// Ported from legacy ExplicitPartials.cs InitArrays.
// ---------------------------------------------------------------------------
struct ExplicitPartials final : Partials {
  ExplicitPartials(uint32_t seed = 0xADD2'0000u) : Partials(seed) { init_arrays_defaults(); }

  const char* type_name() const override { return "ExplicitPartials"; }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"maxPartials", ConfigType::Int,   16.0f, 1.0f, 200.0f},
      {"evolve",      ConfigType::Bool,  1.0f,  0.0f, 1.0f},
      {"unitPO1",     ConfigType::Float, 0.0f,  0.0f, 1.0f},
      {"unitPO2",     ConfigType::Float, 0.0f,  0.0f, 1.0f},
      {"rolloff1",    ConfigType::Float, 1.0f,  0.0f, 10.0f},
      {"rolloff2",    ConfigType::Float, 1.0f,  0.0f, 10.0f},
      {"detune1",     ConfigType::Float, 0.0f,  0.0f, 1.0f},
      {"detune2",     ConfigType::Float, 0.0f,  0.0f, 1.0f},
      {"bandwidth1",  ConfigType::Float, 0.0f,  0.0f, 1.0f},
      {"bandwidth2",  ConfigType::Float, 0.0f,  0.0f, 1.0f},
      {"bandwidthHz", ConfigType::Float, 30.0f, 1.0f, 2000.0f},
      MFORCE_PARTIALS_MOTION_CONFIG_DESCS
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "maxPartials") { maxPartials_ = std::max(1, int(value)); init_arrays_defaults(); return; }
    if (name == "evolve") {
      evolve_ = (value != 0.0f);
      // When turning evolve off, mirror _1 → _2 so rendering stops drifting.
      if (!evolve_) {
        mult2Stat_ = mult1Stat_;
        ampl2Stat_ = ampl1Stat_;
        init_arrays();
      }
      return;
    }
    if (name == "unitPO1")     { unitPO1_ = value; return; }
    if (name == "unitPO2")     { unitPO2_ = value; return; }
    Partials::set_config(name, value);
  }

  float get_config(std::string_view name) const override {
    if (name == "maxPartials") return float(maxPartials_);
    if (name == "evolve")      return evolve_ ? 1.0f : 0.0f;
    if (name == "unitPO1")     return unitPO1_;
    if (name == "unitPO2")     return unitPO2_;
    return Partials::get_config(name);
  }

  // Direct array setters for patch_loader / programmatic use
  void set_arrays(std::vector<float> m1, std::vector<float> m2,
                  std::vector<float> a1, std::vector<float> a2) {
    mult1Stat_ = std::move(m1); mult2Stat_ = std::move(m2);
    ampl1Stat_ = std::move(a1); ampl2Stat_ = std::move(a2);
    maxPartials_ = int(mult1Stat_.size());
    init_arrays();
  }

  // Programmatic setup (backward compat)
  void setup(std::vector<float> m1, std::vector<float> m2,
             std::vector<float> a1, std::vector<float> a2,
             float unitPO1, float unitPO2) {
    unitPO1_ = unitPO1; unitPO2_ = unitPO2;
    set_arrays(std::move(m1), std::move(m2), std::move(a1), std::move(a2));
  }

  // Four parallel arrays grouped as "partials" — UI table keeps them equal
  // length. Column order pairs start/end per partial: Mult1, Ampl1, Mult2, Ampl2.
  // When evolve=false, _2 columns are kept mirrored from _1 (UI disables them).
  std::span<const ArrayDescriptor> array_descriptors() const override {
    static constexpr ArrayDescriptor descs[] = {
      {"mult1", "partials", 1.0f, 0.0f, 200.0f},
      {"ampl1", "partials", 1.0f, 0.0f, 10.0f},
      {"mult2", "partials", 1.0f, 0.0f, 200.0f},
      {"ampl2", "partials", 1.0f, 0.0f, 10.0f},
    };
    return descs;
  }
  void set_array(std::string_view name, std::vector<float> v) override {
    if      (name == "mult1") { mult1Stat_ = std::move(v); if (!evolve_) mult2Stat_ = mult1Stat_; }
    else if (name == "mult2") { mult2Stat_ = std::move(v); }
    else if (name == "ampl1") { ampl1Stat_ = std::move(v); if (!evolve_) ampl2Stat_ = ampl1Stat_; }
    else if (name == "ampl2") { ampl2Stat_ = std::move(v); }
    else return;
    maxPartials_ = int(mult1Stat_.size());
    init_arrays();
  }
  std::vector<float> get_array(std::string_view name) const override {
    // Fall back to the working arrays (init_arrays_defaults populates mult1_
    // etc., not the Stat_ copies) so the UI sees current effective values.
    if (name == "mult1") return mult1Stat_.empty() ? mult1_ : mult1Stat_;
    if (name == "mult2") return mult2Stat_.empty() ? mult2_ : mult2Stat_;
    if (name == "ampl1") return ampl1Stat_.empty() ? ampl1_ : ampl1Stat_;
    if (name == "ampl2") return ampl2Stat_.empty() ? ampl2_ : ampl2Stat_;
    return {};
  }

protected:
  void init_arrays() override {
    if (mult1Stat_.empty()) {
      init_arrays_defaults();
      return;
    }
    mult1_ = mult1Stat_;
    mult2_ = mult2Stat_;
    ampl1_ = ampl1Stat_;
    ampl2_ = ampl2Stat_;

    int n = int(mult1_.size());
    po1_.resize(n);
    po2_.resize(n);
    for (int i = 0; i < n; ++i) {
      po1_[i] = std::fmod((mult1_[i] - 1.0f) * unitPO1_, 1.0f);
      po2_[i] = std::fmod((mult2_[i] - 1.0f) * unitPO2_, 1.0f);
    }
  }

private:
  void init_arrays_defaults() {
    int n = maxPartials_;
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

  int maxPartials_{16};
  bool evolve_{true};
  float unitPO1_{0.0f}, unitPO2_{0.0f};
  // Static copies for re-init after expansion
  std::vector<float> mult1Stat_, mult2Stat_, ampl1Stat_, ampl2Stat_;
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

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"partials", true},  // multi-input: wire multiple Partials sources
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "partials") {
      auto* p = dynamic_cast<IPartials*>(src.get());
      if (p) sets_.push_back({std::move(src), p});
    }
  }

  void add_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    set_param(name, std::move(src));
  }

  void clear_param(std::string_view name) override {
    if (name == "partials") sets_.clear();
  }

  // --- ValueSource interface (not an audio source itself) ---
  void prepare(const RenderContext& ctx, int frames) override { partials_prepare(ctx, frames); }
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
