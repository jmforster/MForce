#pragma once
#include <cmath>
#include <vector>

namespace mforce::dsp {

// One second-order section of a Butterworth cascade, designed by the bilinear
// transform in double precision at `update(cutoff)`: section k of an n-pole
// cascade takes its damping zeta from its pole's angle, and the kind picks
// which of the two coefficient formulas (lowpass or highpass) it runs.
// The per-sample form is Direct Form I over five memories, in the order the
// section's two generic filters computed it: the input scaled by the gain,
// an accumulator started at zero, the three feed-forward products summed in
// index order, then the two feedback products (the start at zero is kept for
// the sign of a zero product).
struct ButterworthSection {
  enum class Kind { Lowpass, Highpass };

  ButterworthSection(Kind kind, int k, int n, float sampleRate)
  : kind_(kind), rate_(sampleRate) {
    zeta_ = float(-std::cos(kPiDesign * (2.0 * k + n - 1.0) / (2.0 * n)));
  }

  void update(float cutoff) {
    if (kind_ == Kind::Lowpass) {
      const float omegac = float(2.0 * rate_ * std::tan(kPiDesign * cutoff / rate_));
      a_[0] = omegac * omegac;
      a_[1] = 2.0f * omegac * omegac;
      a_[2] = omegac * omegac;
      const float b0 = (4.0f * rate_ * rate_) + (4.0f * rate_ * zeta_ * omegac) + (omegac * omegac);
      b_[0] = ((2.0f * omegac * omegac) - (8.0f * rate_ * rate_)) / (-b0);
      b_[1] = ((4.0f * rate_ * rate_) - (4.0f * rate_ * zeta_ * omegac) + (omegac * omegac)) / (-b0);
      gain_ = 1.0f / b0;
    } else {
      const float omegac = float(1.0 / (2.0 * rate_ * std::tan(kPiDesign * cutoff / rate_)));
      a_[0] = 4.0f * rate_ * rate_;
      a_[1] = -8.0f * rate_ * rate_;
      a_[2] = 4.0f * rate_ * rate_;
      const float b0 = (4.0f * rate_ * rate_) + (4.0f * rate_ * zeta_ / omegac) + (1.0f / (omegac * omegac));
      b_[0] = ((2.0f / (omegac * omegac)) - (8.0f * rate_ * rate_)) / (-b0);
      b_[1] = ((4.0f * rate_ * rate_) - (4.0f * rate_ * zeta_ / omegac) + (1.0f / (omegac * omegac))) / (-b0);
      gain_ = 1.0f / b0;
    }
  }

  float process(float input) {
    x_[2] = x_[1];
    x_[1] = x_[0];
    x_[0] = gain_ * input;
    float v = 0.0f;
    v += a_[0] * x_[0];
    v += a_[1] * x_[1];
    v += a_[2] * x_[2];
    float y = v;
    y += b_[0] * y_[0];
    y += b_[1] * y_[1];
    y_[1] = y_[0];
    y_[0] = y;
    return y;
  }

  // Zero the memories; the coefficients stay.
  void reset() {
    x_[0] = x_[1] = x_[2] = 0.0f;
    y_[0] = y_[1] = 0.0f;
  }

private:
  // The design's own pi, to fifteen digits, as the formulas were written: a
  // different double from pi to twenty-one digits, kept so the coefficients
  // keep their bits.
  static constexpr double kPiDesign = 3.14159265358979;

  Kind  kind_;
  float rate_{48000.0f};
  float zeta_{0.0f};
  float a_[3]{}, b_[2]{};
  float gain_{0.0f};
  float x_[3]{}, y_[2]{};
};

// `count` sections of one kind for a cascade of 2·count poles, in order.
inline void build_butterworth_sections(std::vector<ButterworthSection>& sections,
                                       ButterworthSection::Kind kind, int count, float rate) {
  sections.clear();
  for (int i = 0; i < count; ++i)
    sections.emplace_back(kind, i + 1, count * 2, rate);
}

} // namespace mforce::dsp
