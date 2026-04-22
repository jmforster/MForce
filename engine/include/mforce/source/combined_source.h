#pragma once
#include "mforce/core/dsp_value_source.h"
#include <memory>
#include <cmath>
#include <algorithm>

namespace mforce {

// Ported from C# CombinedSource — combines two sources via Add, Multiply,
// Fade, or Sum. Add averages (v1+v2)/2 to preserve headroom; Sum is a
// no-halving variant that's correct when callers want raw addition
// (e.g. modulator layering where amplitudes are chosen to stay bounded).
// Fade: linear crossfade from source1 to source2 over the prepare'd duration.
enum class CombineOp { Add, Multiply, Fade, Sum };

struct CombinedSource final : ValueSource {
  CombineOp op{CombineOp::Add};
  float gainAdj{0.0f};

  CombinedSource(std::shared_ptr<ValueSource> s1, std::shared_ptr<ValueSource> s2,
                 CombineOp operation, float ga = 0.0f)
  : source1_(std::move(s1)), source2_(std::move(s2)), op(operation), gainAdj(ga) {}

  void set_source1(std::shared_ptr<ValueSource> s) { source1_ = std::move(s); }
  void set_source2(std::shared_ptr<ValueSource> s) { source2_ = std::move(s); }
  std::shared_ptr<ValueSource> get_source1() const { return source1_; }
  std::shared_ptr<ValueSource> get_source2() const { return source2_; }

  const char* type_name() const override { return "CombinedSource"; }
  SourceCategory category() const override { return SourceCategory::Combiner; }

  std::span<const ParamDescriptor> param_descriptors() const override { return {}; }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"source1"},
      {"source2"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source1") { source1_ = std::move(src); return; }
    if (name == "source2") { source2_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "source1") return source1_;
    if (name == "source2") return source2_;
    return nullptr;
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"operation", ConfigType::Int,   0.0f, 0.0f, 3.0f},  // 0=Add, 1=Multiply, 2=Fade, 3=Sum
      {"gainAdj",   ConfigType::Float, 0.0f, -1.0f, 10.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "operation") { op = static_cast<CombineOp>(int(value)); return; }
    if (name == "gainAdj")   { gainAdj = value; return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "operation") return float(static_cast<int>(op));
    if (name == "gainAdj")   return gainAdj;
    return 0.0f;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    totalFrames_ = frames;
    ptr_ = -1;
    source1_->prepare(ctx, frames);
    source2_->prepare(ctx, frames);
  }

  float next() override {
    ++ptr_;
    source1_->next();
    source2_->next();

    float v1 = source1_->current();
    float v2 = source2_->current();

    if (op == CombineOp::Add) {
      cur_ = (v1 + v2 * (1.0f + gainAdj)) * 0.5f;
    } else if (op == CombineOp::Multiply) {
      cur_ = v1 * v2 * (1.0f + gainAdj);
    } else if (op == CombineOp::Sum) {
      cur_ = v1 + v2 * (1.0f + gainAdj);
    } else { // Fade
      float t = (totalFrames_ > 0) ? float(ptr_) / float(totalFrames_) : 0.0f;
      cur_ = v1 * (1.0f - t) + v2 * (1.0f + gainAdj) * t;
    }
    return cur_;
  }

  float current() const override { return cur_; }

private:
  std::shared_ptr<ValueSource> source1_;
  std::shared_ptr<ValueSource> source2_;
  int ptr_{-1};
  int totalFrames_{1};
  float cur_{0.0f};
};

// Ported from C# CrossfadeSource — plays source1, then crossfades into source2.
// ratio: fraction of total time for source1 (0.7 = 70% source1, 30% source2)
// overlap: fraction of the shorter segment that overlaps during transition
struct CrossfadeSource final : ValueSource {
  float ratio{0.5f};
  float overlap{0.1f};
  float gainAdj{0.0f};

  void set_source1(std::shared_ptr<ValueSource> s)   { source1_ = std::move(s); }
  void set_source2(std::shared_ptr<ValueSource> s)   { source2_ = std::move(s); }
  void set_amplitude(std::shared_ptr<ValueSource> s) { amplitude_ = std::move(s); }
  std::shared_ptr<ValueSource> get_source1() const   { return source1_; }
  std::shared_ptr<ValueSource> get_source2() const   { return source2_; }
  std::shared_ptr<ValueSource> get_amplitude() const { return amplitude_; }

  const char* type_name() const override { return "CrossfadeSource"; }
  SourceCategory category() const override { return SourceCategory::Combiner; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"amplitude", 1.0f,  0.0f,     10.0f},
    };
    return descs;
  }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"source1"},
      {"source2"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source1")   { source1_ = std::move(src); return; }
    if (name == "source2")   { source2_ = std::move(src); return; }
    if (name == "amplitude") { amplitude_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "source1")   return source1_;
    if (name == "source2")   return source2_;
    if (name == "amplitude") return amplitude_;
    return nullptr;
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"ratio",   ConfigType::Float, 0.5f, 0.0f, 1.0f},
      {"overlap", ConfigType::Float, 0.1f, 0.0f, 1.0f},
      {"gainAdj", ConfigType::Float, 0.0f, -1.0f, 10.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "ratio")   { ratio = value; return; }
    if (name == "overlap") { overlap = value; return; }
    if (name == "gainAdj") { gainAdj = value; return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "ratio")   return ratio;
    if (name == "overlap") return overlap;
    if (name == "gainAdj") return gainAdj;
    return 0.0f;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    totalFrames_ = frames;
    ptr_ = -1;

    int s1Count = int(std::lround(double(frames) * ratio));
    int s2Count = frames - s1Count;
    overlapCount_ = int(std::lround(overlap * double(std::min(s1Count, s2Count))));

    s1End_ = s1Count + overlapCount_;
    s2Start_ = s1Count - overlapCount_;

    if (amplitude_) amplitude_->prepare(ctx, frames);
    source1_->prepare(ctx, s1Count + overlapCount_);
    source2_->prepare(ctx, s2Count + overlapCount_);
  }

  float next() override {
    ++ptr_;
    float amp = amplitude_ ? (amplitude_->next(), amplitude_->current()) : 1.0f;

    if (ptr_ < s2Start_) {
      cur_ = source1_->next() * amp;
    } else if (ptr_ >= s1End_) {
      cur_ = source2_->next() * amp * (1.0f + gainAdj);
    } else {
      float v1 = source1_->next();
      float v2 = source2_->next();
      float t = float(ptr_ - s2Start_) / float(s1End_ - s2Start_);
      cur_ = (v1 * (1.0f - t) + v2 * (1.0f + gainAdj) * t) * amp;
    }
    return cur_;
  }

  float current() const override { return cur_; }

private:
  std::shared_ptr<ValueSource> source1_;
  std::shared_ptr<ValueSource> source2_;
  std::shared_ptr<ValueSource> amplitude_;
  int totalFrames_{1};
  int ptr_{-1};
  int overlapCount_{0};
  int s1End_{0}, s2Start_{0};
  float cur_{0.0f};
};

// Ported from C# DistortedSource — waveshaping distortion.
// Shifts, amplifies, and clips the input. Density controls probability of effect.
struct DistortedSource final : ValueSource {
  explicit DistortedSource(uint32_t seed = 0xD1570000u) : rng_(seed) {}

  void set_source(std::shared_ptr<ValueSource> s)    { source_ = std::move(s); }
  void set_amplitude(std::shared_ptr<ValueSource> s) { amplitude_ = std::move(s); }
  void set_density(std::shared_ptr<ValueSource> s)   { density_ = std::move(s); }
  void set_gain(std::shared_ptr<ValueSource> s)      { gain_ = std::move(s); }
  void set_shift(std::shared_ptr<ValueSource> s)     { shift_ = std::move(s); }

  std::shared_ptr<ValueSource> get_source() const    { return source_; }
  std::shared_ptr<ValueSource> get_amplitude() const { return amplitude_; }
  std::shared_ptr<ValueSource> get_density() const   { return density_; }
  std::shared_ptr<ValueSource> get_gain() const      { return gain_; }
  std::shared_ptr<ValueSource> get_shift() const     { return shift_; }

  const char* type_name() const override { return "DistortedSource"; }
  SourceCategory category() const override { return SourceCategory::Modulator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"amplitude", 1.0f,  0.0f,     10.0f},
      {"density",   1.0f,  0.0f,     1.0f},
      {"gain",      1.0f,  0.0f,     100.0f},
      {"shift",     0.0f, -1.0f,     1.0f},
    };
    return descs;
  }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"source"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source")    { source_ = std::move(src); return; }
    if (name == "amplitude") { amplitude_ = std::move(src); return; }
    if (name == "density")   { density_ = std::move(src); return; }
    if (name == "gain")      { gain_ = std::move(src); return; }
    if (name == "shift")     { shift_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "source")    return source_;
    if (name == "amplitude") return amplitude_;
    if (name == "density")   return density_;
    if (name == "gain")      return gain_;
    if (name == "shift")     return shift_;
    return nullptr;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    if (source_) source_->prepare(ctx, frames);
    if (amplitude_) amplitude_->prepare(ctx, frames);
    if (density_) density_->prepare(ctx, frames);
    if (gain_) gain_->prepare(ctx, frames);
    if (shift_) shift_->prepare(ctx, frames);
  }

  float next() override {
    if (source_) source_->next();
    if (density_) density_->next();
    if (gain_) gain_->next();
    if (shift_) shift_->next();
    if (amplitude_) amplitude_->next();

    float s = source_ ? source_->current() : 0.0f;
    float d = density_ ? density_->current() : 1.0f;
    float g = gain_ ? gain_->current() : 1.0f;
    float sh = shift_ ? shift_->current() : 0.0f;
    float a = amplitude_ ? amplitude_->current() : 1.0f;

    if (rng_.decide(d)) {
      float shifted = s + sh;
      cur_ = std::min(std::fabs(shifted) * (1.0f + g), 1.0f)
           * (shifted >= 0.0f ? 1.0f : -1.0f) * a;
    } else {
      cur_ = 0.0f;
    }
    return cur_;
  }

  float current() const override { return cur_; }

private:
  std::shared_ptr<ValueSource> source_;
  std::shared_ptr<ValueSource> amplitude_;
  std::shared_ptr<ValueSource> density_;
  std::shared_ptr<ValueSource> gain_;
  std::shared_ptr<ValueSource> shift_;
  Randomizer rng_;
  float cur_{0.0f};
};

// Ported from C# StaticVarSource — random value set once on prepare().
// value = baseValue * (1 + random * varPct), optionally biased.
struct StaticVarSource final : ValueSource {
  float baseValue{1.0f};
  float varPct{0.0f};
  float bias{0.0f};

  StaticVarSource(float base, float pct, float b = 0.0f, uint32_t seed = 0x57A70001u)
  : baseValue(base), varPct(pct), bias(b), rng_(seed) {}

  const char* type_name() const override { return "StaticVarSource"; }
  SourceCategory category() const override { return SourceCategory::Utility; }

  void prepare(const RenderContext& /*ctx*/, int /*frames*/) override {
    float r = rng_.valuePN() * varPct;
    if (bias != 0.0f) r = r * (1.0f - std::fabs(bias)) + bias * varPct;
    cur_ = baseValue * (1.0f + r);
  }

  float next() override { return cur_; }
  float current() const override { return cur_; }

private:
  Randomizer rng_;
  float cur_{0.0f};
};

// Ported from C# StaticRangeSource — random value in [min,max] set once on prepare().
struct StaticRangeSource final : ValueSource {
  float min{0.0f}, max{1.0f}, bias{0.0f};

  StaticRangeSource(float mn, float mx, float b = 0.0f, uint32_t seed = 0x57A70002u)
  : min(mn), max(mx), bias(b), rng_(seed) {}

  const char* type_name() const override { return "StaticRangeSource"; }
  SourceCategory category() const override { return SourceCategory::Utility; }

  void prepare(const RenderContext& /*ctx*/, int /*frames*/) override {
    cur_ = rng_.range(min, max, bias);
  }

  float next() override { return cur_; }
  float current() const override { return cur_; }

private:
  Randomizer rng_;
  float cur_{0.0f};
};

} // namespace mforce
