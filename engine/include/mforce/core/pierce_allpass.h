#pragma once
#include <algorithm>
#include <cmath>

namespace mforce {

// ---------------------------------------------------------------------------
// PierceAllpass — the Pierce/Van Duyne passive nonlinear filter.
//
// Pierce & Van Duyne, "A passive nonlinear digital filter design which
// facilitates physics-based sound synthesis of highly nonlinear musical
// instruments", JASA 101(2) 1120-1126 (1997); structure recovered from US
// Patent 5,703,313 A and from JOS's Faust `fi.apnl`, which cites the paper.
// Full derivation, provenance and the measured numbers:
// docs/research/stk_port/PIERCE_PASSIVE_NOTES.md.
//
// A first-order allpass whose coefficient is a SPRING STIFFNESS seen through
// a waveguide of impedance R0, a = (k - alpha*R0)/(k + alpha*R0), so a runs
// -1 (free) to +1 (rigid).  The nonlinearity is asymmetry: one stiffness while
// the spring's displacement is negative (`aNeg`, the patent's a_1), another
// while it is zero or positive (`aPos`, the patent's a_2).  Equal coefficients
// = an exactly linear allpass.
//
// The patent's recurrence is
//     u(n) = x(n) - a(n-1) u(n-1),  a(n) = aNeg if u(n)<0 else aPos,
//     y(n) = a(n) u(n) + u(n-1)
// and its passivity argument is a TIMING argument: the stiffness is swapped
// where the spring force (u(n)+u(n-1))/2 crosses zero, so no stored energy is
// rescaled.  That is exact in continuous time only.  In discrete time the
// switching sample straddles the crossing rather than landing on it, and the
// literal recurrence measurably CREATES energy — worst cumulative out/in
// energy 2.43 over the probe set, and 1.48 / 4.76 for the two other readings
// of the same equations.  So this implementation keeps the structure and the
// switching rule and makes the stiffness change exactly energy-preserving:
//
//     u(n) = x(n) - a u(n-1)                        <- a held over the sample
//     y(n) = a u(n) + u(n-1)                           => y^2 = x^2 + E(n-1)-E(n)
//     a'   = aNeg if u(n) < 0 else aPos                  identically, where
//     if a' != a:                                        E = (1-a^2) u^2
//         u(n) *= sqrt((1-a^2)/(1-a'^2))            <- same stored energy
//         a     = a'
//
// which is lossless by construction on both halves, hence passive for ANY
// coefficient pair: measured cumulative out/in energy 1.000000000.  The
// rescale multiplies a state that is near zero exactly where the paper says
// the stored energy is negligible, so the two forms coincide in the limit;
// when |aNeg| == |aPos| (including Faust's own +-a example) the factor is
// exactly 1.  Spectral spreading is unaffected: 300 Hz sine in, content above
// 400 Hz out at -10.8 dB re fundamental for both forms.
//
// Being passive is the point — no half-drive normalisation and no runaway
// clamp, unlike the time-varying-allpass stand-in of mesh2d edgeMode 1.
//
// State and coefficient are double so that switching (which can happen on
// every other sample) cannot accumulate a float32 bias inside a feedback
// loop; the interface is float.  POD, no allocation, trivially resettable.
// ---------------------------------------------------------------------------
struct PierceAllpass {
  // 1 - a^2 >= ~2e-3, so the energy rescale can never divide by zero.
  static constexpr float kCoefMax = 0.999f;

  static float clamp_coef(float a) {
    return std::clamp(a, -kCoefMax, kCoefMax);
  }

  // u1 = 0 means the next state decision sees u >= 0, so `a` starts at aPos.
  void reset(float aPos) {
    u1_ = 0.0;
    a_ = double(clamp_coef(aPos));
  }

  float tick(float x, float aNeg, float aPos) {
    const double u = double(x) - a_ * u1_;
    const double y = a_ * u + u1_;
    const double an = double(clamp_coef(u < 0.0 ? aNeg : aPos));
    if (an != a_) {
      // Stiffness change at constant stored energy E = (1 - a^2) u^2.
      u1_ = u * std::sqrt((1.0 - a_ * a_) / (1.0 - an * an));
      a_ = an;
    } else {
      u1_ = u;
    }
    return float(y);
  }

  // Stored energy, for the passivity gate: x^2 - y^2 == E(n) - E(n-1).
  double energy() const { return (1.0 - a_ * a_) * u1_ * u1_; }

private:
  double u1_{0.0};
  double a_{0.0};
};

} // namespace mforce
