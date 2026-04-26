#pragma once
#include "mforce/core/dsp_value_source.h"
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
                                  int index, IFormant* formant, float formantWeight) = 0;
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
  , dtEnv_(std::make_shared<ConstantSource>(0.0f)) {}

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
    };
    return descs;
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"rolloff1", ConfigType::Float, 1.0f, 0.0f, 10.0f},
      {"rolloff2", ConfigType::Float, 1.0f, 0.0f, 10.0f},
      {"detune1",  ConfigType::Float, 0.0f, 0.0f, 1.0f},
      {"detune2",  ConfigType::Float, 0.0f, 0.0f, 1.0f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "multEnv") { multEnv_ = std::move(src); return; }
    if (name == "amplEnv") { amplEnv_ = std::move(src); return; }
    if (name == "poEnv")   { poEnv_ = std::move(src); return; }
    if (name == "roEnv")   { roEnv_ = std::move(src); return; }
    if (name == "dtEnv")   { dtEnv_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "multEnv") return multEnv_;
    if (name == "amplEnv") return amplEnv_;
    if (name == "poEnv")   return poEnv_;
    if (name == "roEnv")   return roEnv_;
    if (name == "dtEnv")   return dtEnv_;
    return nullptr;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "rolloff1") { ro1_ = value; return; }
    if (name == "rolloff2") { ro2_ = value; return; }
    if (name == "detune1")  { dt1_ = value; return; }
    if (name == "detune2")  { dt2_ = value; return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "rolloff1") return ro1_;
    if (name == "rolloff2") return ro2_;
    if (name == "detune1")  return dt1_;
    if (name == "detune2")  return dt2_;
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

    init_detune_values();
  }

  void partials_next() override {
    multEnv_->next();
    amplEnv_->next();
    poEnv_->next();
    roEnv_->next();
    dtEnv_->next();
  }

  int partial_count() const override {
    return int(mult1_.size());
  }

  // Port of legacy Partials.cs GetPartialValue — the authoritative per-partial math
  float get_partial_value(float amplitude, float frequency, float phaseDiff,
                          int index, IFormant* formant, float fmtWt) override
  {
    float multE = multEnv_->current();
    float amplE = amplEnv_->current();
    float poE   = poEnv_->current();
    float roE   = roEnv_->current();
    float dtE   = dtEnv_->current();

    // Multiplier (can evolve between mult1 and mult2)
    float pmult = mult1_[index] + (mult2_[index] - mult1_[index]) * multE;

    // Phase offset (can evolve)
    float ppo = po1_[index] + (po2_[index] - po1_[index]) * poE;

    // Frequency = multiplier * base freq * (1 + detune)
    float dt = (dt1_ + (dt2_ - dt1_) * dtE) * dtVals_[index];
    float pfreq = pmult * frequency * (1.0f + dt);

    // Past cutoff -> NaN signal to caller
    if (pfreq > CUTOFF) return std::numeric_limits<float>::quiet_NaN();

    constexpr float TAU = 2.0f * 3.14159265358979323846f;

    // Advance partial position (legacy Partials.cs lines 197-200)
    partialPos_[index] = std::fmod(
        partialPos_[index] + pfreq / rate_ + phaseDiff + (ppo - partialLPO_[index]),
        1.0f);
    if (partialPos_[index] < 0.0f) partialPos_[index] += 1.0f;
    partialLPO_[index] = ppo;

    // Amplitude with rolloff (legacy lines 206-216)
    float ro = ro1_ + (ro2_ - ro1_) * roE;
    float rolloff = (ro == 0.0f) ? 1.0f : (1.0f / std::pow(pmult, ro));

    // Formant factor
    float fmtFactor = 1.0f;
    if (formant && fmtWt > 0.0f) {
      if (formant->contains(pfreq)) {
        fmtFactor = 1.0f - fmtWt + fmtWt * formant->get_gain(pfreq);
      } else {
        fmtFactor = 1.0f - fmtWt;
      }
    }

    // Fade out partials near cutoff (within 1000 Hz) — legacy line 216
    float fade = (pfreq < CUTOFF - 1000.0f) ? 1.0f : (CUTOFF - pfreq) / 1000.0f;

    float pampl = amplitude *
        (ampl1_[index] + (ampl2_[index] - ampl1_[index]) * amplE) *
        rolloff * fmtFactor * fade;

    return std::sin(partialPos_[index] * TAU) * pampl;
  }

  // Read-only access to the live partial arrays. Subclasses (FullPartials,
  // SequencePartials) populate these via init_arrays(). ExplicitPartials
  // overrides to prefer its user-edited Stat_ copies. Used by the UI's
  // partials strip view to render the bar chart.
  std::vector<float> get_array(std::string_view name) const override {
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

  Randomizer rng_;

  // Envelopes
  std::shared_ptr<ValueSource> multEnv_;
  std::shared_ptr<ValueSource> amplEnv_;
  std::shared_ptr<ValueSource> poEnv_;
  std::shared_ptr<ValueSource> roEnv_;
  std::shared_ptr<ValueSource> dtEnv_;

  // Global rolloff/detune ranges
  float ro1_{1.0f}, ro2_{1.0f};
  float dt1_{0.0f}, dt2_{0.0f};

  // Per-partial static arrays (set by init_arrays in subclass)
  std::vector<float> mult1_, mult2_, ampl1_, ampl2_, po1_, po2_;

  // Per-partial runtime state
  std::vector<float> partialPos_, partialPO_, partialLPO_, dtVals_;

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
                          int index, IFormant* formant, float formantWeight) override {
    int offset = 0;
    for (auto& e : sets_) {
      int count = e.ipartials->partial_count();
      if (index < offset + count)
        return e.ipartials->get_partial_value(amplitude, frequency, phaseDiff,
                                               index - offset, formant, formantWeight);
      offset += count;
    }
    return 0.0f;
  }

private:
  struct Entry {
    std::shared_ptr<ValueSource> source;  // prevents the shared_ptr from dying
    IPartials* ipartials;                  // fast pointer to the IPartials interface
  };
  std::vector<Entry> sets_;
};

} // namespace mforce
