#pragma once

namespace mforce::dsp {

// The first-order allpass y = c·x + x1 − c·y1. With c = (1 − d)/(1 + d) it
// delays by d samples at low frequency (Thiran's first order), which is how a
// loop whose length must be an integer number of samples lands on pitch: the
// integer part is the delay line, the fraction is this section. The same
// section with a coefficient chosen for its phase slope is a dispersion
// filter (EKS's string stiffness). The owner decides whether to run it at
// all; a coefficient of 0 makes it a unit delay, not a bypass.
struct TuningAllpass {
  void set_fraction(float frac) { c_ = (1.0f - frac) / (1.0f + frac); }
  void set_coefficient(float c) { c_ = c; }
  float coefficient() const { return c_; }
  void reset() { x1_ = y1_ = 0.0f; }
  float process(float x) {
    const float y = c_ * x + x1_ - c_ * y1_;
    x1_ = x;
    y1_ = y;
    return y;
  }

private:
  float c_{0.0f};
  float x1_{0.0f}, y1_{0.0f};
};

} // namespace mforce::dsp
