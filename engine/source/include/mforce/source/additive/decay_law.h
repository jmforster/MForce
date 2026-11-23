#pragma once
// DecayLaw: the per-partial exponential decay of the piano law.
#include <cmath>
#include <vector>

namespace mforce {

// Per-partial exponential decay: rate_i = decayRate * pmult_i^decayExp in
// dB per second — decayRate is the fundamental's rate and decayExp tilts
// the law across the spectrum (a measured piano within-note exponent of
// about 0.6: high partials die faster). Each partial's per-sample factor is
// derived from its effective multiplier; the running gain is advanced once
// per sample and read once per partial. A value type the owner holds:
// prepare sizes it, start resets the gains to unity, set_gain derives one
// partial's factor, advance steps every running gain.
struct DecayLaw {
  void prepare(int n) {
    gain_.assign(size_t(n), 0.0f);
    state_.assign(size_t(n), 0.0f);
  }

  // A note with the law on: every running gain back to unity. Fills within
  // the capacity prepare gave it.
  void start(int n) {
    gain_.assign(size_t(n), 1.0f);
    state_.assign(size_t(n), 1.0f);
  }

  // rate_i = decayRate * pmult^decayExp dB/s -> per-sample amplitude factor
  // 10^(-rate_i / (20*sr)). Double intermediates: the factor is within ~1e-7
  // of 1, where float pow quantisation would bias the long-run decay audibly.
  void set_gain(int index, float pmult, float decayRate, float decayExp, float sampleRate) {
    const double r = double(decayRate) * std::pow(double(pmult), double(decayExp));
    gain_[size_t(index)] = float(std::pow(10.0, -r / (20.0 * double(sampleRate))));
  }

  void advance() {
    const size_t n = state_.size();
    for (size_t i = 0; i < n; ++i) state_[i] *= gain_[i];
  }

  float gain(int index) const { return state_[size_t(index)]; }

private:
  std::vector<float> gain_, state_;   // the per-sample factor, the running gain
};

} // namespace mforce
