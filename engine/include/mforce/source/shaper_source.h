#pragma once
#include "mforce/core/curve.h"
#include "mforce/core/dsp_value_source.h"
#include <algorithm>
#include <memory>
#include <vector>

namespace mforce {

// Per-sample drawn-curve transfer: y = curve(drive * x) — the junction
// nonlinearity for feedback loops (feedback_loop_design.md §3.5), and a
// free waveshaper open-loop. values[] holds ABSOLUTE (x, y) breakpoints
// ascending in x (NOT the SegmentSource delta form); evaluation is
// piecewise between breakpoints through SmoothnessInterpolator, clamped
// to the end values outside the drawn range. Default = identity.
struct ShaperSource final : ValueSource {
  ShaperSource()
    : source_(std::make_shared<ConstantSource>(0.0f)),
      drive_(std::make_shared<ConstantSource>(1.0f)),
      smoothness_(std::make_shared<ConstantSource>(0.5f)),
      values_{-1.0f, -1.0f, 1.0f, 1.0f} {}

  const char* type_name() const override { return "Shaper"; }
  SourceCategory category() const override { return SourceCategory::Modulator; }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"source"},
    };
    return descs;
  }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"drive",      1.0f, 0.0f, 20.0f, "ratio"},
      {"smoothness", 0.5f, 0.0f, 1.0f, "0-1"},
      // Point-space blend toward curve B (values2/segs2); 0 = pure A =
      // byte-identical legacy path. Curve-morph spec §5.
      {"morph",      0.0f, 0.0f, 1.0f, "0-1"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source")     { source_ = std::move(src); return; }
    if (name == "drive")      { drive_ = std::move(src); return; }
    if (name == "smoothness") { smoothness_ = std::move(src); return; }
    if (name == "morph")      { morph_ = std::move(src); return; }
  }
  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "source")     return source_;
    if (name == "drive")      return drive_;
    if (name == "smoothness") return smoothness_;
    if (name == "morph")      return morph_;
    return nullptr;
  }

  std::span<const ArrayDescriptor> array_descriptors() const override {
    static constexpr ArrayDescriptor descs[] = {
      {"values",  nullptr, 0.0f, -2.0f, 2.0f},
      // Per-segment overrides (curve-morph spec §3): flat [typeIdx, power]
      // per segment; typeIdx 0 = default (smoothness pin), else
      // 1 + int(RampType) (1 Linear, 2 Expo, 3 InverseExpo, 4 Sine,
      // 5 Hold). Empty = all default (existing patches untouched).
      {"segs",    nullptr, 0.0f,  0.0f, 40.0f},
      // Morph curve B (spec §5): same shapes as values/segs; empty =
      // single-curve Shaper. Inert until the morph pin lands.
      {"values2", nullptr, 0.0f, -2.0f, 2.0f},
      {"segs2",   nullptr, 0.0f,  0.0f, 40.0f},
    };
    return descs;
  }

  static std::vector<Curve::Seg> decode_segs(const std::vector<float>& v) {
    std::vector<Curve::Seg> out;
    for (size_t i = 0; i + 1 < v.size(); i += 2) {
      Curve::Seg s;
      const int t = int(v[i]);
      if (t >= 1 && t <= 5) {
        s.overridden = true;
        s.type = RampType(t - 1);
        s.power = v[i + 1];
      }
      out.push_back(s);
    }
    return out;
  }
  static std::vector<float> encode_segs(const std::vector<Curve::Seg>& segs) {
    bool any = false;
    for (const auto& s : segs) any |= s.overridden;
    if (!any) return {};
    std::vector<float> v;
    v.reserve(segs.size() * 2);
    for (const auto& s : segs) {
      v.push_back(s.overridden ? float(1 + int(s.type)) : 0.0f);
      v.push_back(s.power);
    }
    return v;
  }

  void set_array(std::string_view name, std::vector<float> v) override {
    if (name == "values" && v.size() >= 4 && v.size() % 2 == 0) {
      values_ = std::move(v);
      return;
    }
    if (name == "segs")    { segs_  = decode_segs(v); return; }
    if (name == "values2") {
      if (v.empty() || (v.size() >= 4 && v.size() % 2 == 0))
        valuesB_ = std::move(v);
      return;
    }
    if (name == "segs2")   { segsB_ = decode_segs(v); return; }
  }
  std::vector<float> get_array(std::string_view name) const override {
    if (name == "values")  return values_;
    if (name == "segs")    return encode_segs(segs_);
    if (name == "values2") return valuesB_;
    if (name == "segs2")   return encode_segs(segsB_);
    return {};
  }

  void prepare(const RenderContext& ctx, int frames) override {
    if (source_) source_->prepare(ctx, frames);
    drive_->prepare(ctx, frames);
    smoothness_->prepare(ctx, frames);
    if (morph_) morph_->prepare(ctx, frames);
    cur_ = 0.0f;
  }

  float next() override {
    const float x = (source_ ? source_->next() : 0.0f) * drive_->next();
    smoothness_->next();
    smoothCur_ = smoothness_->current();
    if (morph_) { morph_->next(); morphCur_ = morph_->current(); }
    cur_ = map(x);
    return cur_;
  }

  float current() const override { return cur_; }

  // Public for the UI preview/editor overlay. Delegates to the shared
  // evaluator (curve.h). With curve B present and morph in (0,1], the
  // breakpoints (and matching segment powers) lerp point-space — the knee
  // slides rather than smearing; ascending x is preserved because the
  // lerp of two ascending lists is ascending. Segment TYPES come from
  // segs_ (shared structure; the loader rejects a segs2 whose types
  // differ). Mismatched point counts degrade to pure A.
  float map(float x) {
    const bool canMorph = !valuesB_.empty()
                       && valuesB_.size() == values_.size();
    const float m = canMorph ? std::clamp(morphCur_, 0.0f, 1.0f) : 0.0f;
    if (m <= 0.0f)
      return Curve::eval_flat(values_, segs_, Curve::Domain::Linear,
                              smoothCur_, x);
    const auto& A = values_;
    const auto& B = valuesB_;
    auto lp = [m](float a, float b) { return a + (b - a) * m; };
    return Curve::eval_core(A.size() / 2,
        [&](size_t i) { return lp(A[i * 2],     B[i * 2]);     },
        [&](size_t i) { return lp(A[i * 2 + 1], B[i * 2 + 1]); },
        [&](size_t i) {
            Curve::Seg s = i < segs_.size() ? segs_[i] : Curve::Seg{};
            if (s.overridden && i < segsB_.size() && segsB_[i].overridden) {
                const Curve::Seg& b = segsB_[i];
                const bool aPow = s.type == RampType::Expo
                               || s.type == RampType::InverseExpo;
                const bool bPow = b.type == RampType::Expo
                               || b.type == RampType::InverseExpo;
                if (aPow && bPow && s.type != b.type) {
                    // Bulge direction is geometry, not structure: Expo and
                    // InverseExpo are one signed-curvature axis (Expo
                    // negative). Lerp the signed log-power so the curve
                    // flattens through linear and bulges out the other side.
                    auto c = [](const Curve::Seg& g) {
                        const float lg = std::log2(std::max(g.power, 1e-3f));
                        return g.type == RampType::InverseExpo ? lg : -lg;
                    };
                    const float cm = lp(c(s), c(b));
                    s.type = cm >= 0.0f ? RampType::InverseExpo
                                        : RampType::Expo;
                    s.power = std::pow(2.0f, std::fabs(cm));
                } else {
                    s.power = lp(s.power, b.power);
                }
            }
            return s;
        },
        Curve::Domain::Linear, smoothCur_, x);
  }

private:
  std::shared_ptr<ValueSource> source_, drive_, smoothness_, morph_;
  float morphCur_{0.0f};
  std::vector<float> values_;
  std::vector<Curve::Seg> segs_;
  std::vector<float> valuesB_;        // morph curve B points (empty = none)
  std::vector<Curve::Seg> segsB_;     // morph curve B segment powers
  float smoothCur_{0.5f};
  float cur_{0.0f};
};

} // namespace mforce
