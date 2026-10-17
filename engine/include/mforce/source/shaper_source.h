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
struct ShaperSource final : Processor {
  Param drive     {this, "drive",      1.0f, 0.0f, 20.0f, "ratio"};
  Param smoothness{this, "smoothness", 0.5f, 0.0f, 1.0f,  "0-1"};
  // Point-space blend toward curve B (values2/segs2); 0 = pure A =
  // byte-identical legacy path. Curve-morph spec §5.
  Param morph     {this, "morph",      0.0f, 0.0f, 1.0f,  "0-1"};
  // Hysteresis thresholds, in post-drive curve-x units (hysteresis spec §2).
  // breakaway ~ bow pressure; capture 0 = zero-cross-only recapture. Read
  // only in hysteresis mode, so the node pulls them itself (Manual): the
  // base neither prepares nor advances a Manual pin.
  Param breakaway {this, "breakaway",  0.6f, 0.0f, 2.0f,  "curve-x", Pull::Manual};
  Param capture   {this, "capture",    0.0f, 0.0f, 2.0f,  "curve-x", Pull::Manual};
  // Stick/slip junction mode (hysteresis spec §3): curve A = stick, curve B
  // = slip, one bit of state. Mutually exclusive with morph (morph is
  // ignored while on; loader warns).
  Setting<bool> hysteresis{this, "hysteresis", false, false, true};
  Array values {this, "values",  nullptr, 0.0f, -2.0f, 2.0f};
  // Per-segment overrides (curve-morph spec §3): flat [typeIdx, power]
  // per segment; typeIdx 0 = default (smoothness pin), else
  // 1 + int(RampType) (1 Linear, 2 Expo, 3 InverseExpo, 4 Sine,
  // 5 Hold). Empty = all default (existing patches untouched).
  Array segs   {this, "segs",    nullptr, 0.0f, 0.0f,  40.0f};
  // Morph curve B (spec §5): same shapes as values/segs; empty =
  // single-curve Shaper. Inert until the morph pin lands.
  Array values2{this, "values2", nullptr, 0.0f, -2.0f, 2.0f};
  Array segs2  {this, "segs2",   nullptr, 0.0f, 0.0f,  40.0f};

  ShaperSource() : values_{-1.0f, -1.0f, 1.0f, 1.0f} { values.values = values_; }

  const char* type_name() const override { return "Shaper"; }
  SourceCategory category() const override { return SourceCategory::Modulator; }

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
  static std::vector<float> encode_segs(const std::vector<Curve::Seg>& decoded) {
    bool any = false;
    for (const auto& s : decoded) any |= s.overridden;
    if (!any) return {};
    std::vector<float> v;
    v.reserve(decoded.size() * 2);
    for (const auto& s : decoded) {
      v.push_back(s.overridden ? float(1 + int(s.type)) : 0.0f);
      v.push_back(s.power);
    }
    return v;
  }

  // A curve of fewer than two points, or one with an odd number of floats,
  // is refused: the array is rolled back to the previous curve. The segment
  // arrays are held normalised, as encode_segs writes them.
  void on_array_changed(std::string_view name) override {
    if (name == "values") {
      if (accepts_curve(values.values)) values_ = values.values;
      else values.values = values_;
    } else if (name == "segs") {
      segs_ = decode_segs(segs.values);
      segs.values = encode_segs(segs_);
    } else if (name == "values2") {
      if (values2.values.empty() || accepts_curve(values2.values)) valuesB_ = values2.values;
      else values2.values = valuesB_;
    } else if (name == "segs2") {
      segsB_ = decode_segs(segs2.values);
      segs2.values = encode_segs(segsB_);
    }
  }

  void on_prepare(const RenderContext& ctx, int frames) override {
    if (breakaway.source) breakaway.source->prepare(ctx, frames);
    if (capture.source)   capture.source->prepare(ctx, frames);
    stuck_ = true;      // bow resting on the string at note start
    lastSign_ = 0;
    cur_ = 0.0f;
  }

  float compute() override {
    const float x = input.current() * drive.current();
    smoothCur_ = smoothness.current();
    morphCur_ = morph.current();
    if (hysteresis.value) {
      breakaway.advance();
      capture.advance();
      const float ba = breakaway.current();
      const float cap = capture.current();
      if (stuck_) {
        if (std::fabs(x) > ba) stuck_ = false;
      } else if ((lastSign_ != 0 && x != 0.0f
                  && (x > 0.0f) != (lastSign_ > 0))
                 || std::fabs(x) < cap) {
        stuck_ = true;   // sign reversal (or capture band) re-sticks —
                         // compared against the last NONZERO sign, so a
                         // sample landing exactly on 0 can't swallow the
                         // crossing (loops sit at exact 0 routinely)
      }
      if (x > 0.0f)      lastSign_ = 1;
      else if (x < 0.0f) lastSign_ = -1;
      // Slip curve = values2; absent/short values2 leaves the mode inert
      // (loader warns at load).
      const bool useB = !stuck_ && valuesB_.size() >= 4;
      return useB
        ? Curve::eval_flat(valuesB_, segsB_, Curve::Domain::Linear,
                           smoothCur_, x)
        : Curve::eval_flat(values_, segs_, Curve::Domain::Linear,
                           smoothCur_, x);
    }
    return map(x);
  }

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
  static bool accepts_curve(const std::vector<float>& v) {
    return v.size() >= 4 && v.size() % 2 == 0;
  }

  bool stuck_{true};
  int lastSign_{0};
  float morphCur_{0.0f};
  std::vector<float> values_;
  std::vector<Curve::Seg> segs_;
  std::vector<float> valuesB_;        // morph curve B points (empty = none)
  std::vector<Curve::Seg> segsB_;     // morph curve B segment powers
  float smoothCur_{0.5f};
};

} // namespace mforce
