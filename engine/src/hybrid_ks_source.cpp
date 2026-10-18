#include "mforce/source/hybrid_ks_source.h"
#include <algorithm>
#include <cmath>

namespace mforce {

static constexpr float TAU = 2.0f * 3.14159265358979323846f;

HybridKSSource::HybridKSSource(uint32_t seed)
: rng_(seed) {
  inputSource.source = std::make_shared<WhiteNoiseSource>(seed);
}

// The target array holds an entry per partial the additive phase reads.
void HybridKSSource::size_targets() {
  if (int(targetAmpl_.size()) < numPartials.value)
    targetAmpl_.resize(numPartials.value, 0.0f);
}

void HybridKSSource::on_prepare(const RenderContext& ctx) {
  inputSource.prepare(ctx);
  size_targets();
}

void HybridKSSource::on_setting_changed(std::string_view name) {
  if (name == "numPartials" && prepared()) size_targets();
}

void HybridKSSource::on_start(int frames) {
  WaveSource::on_start(frames);
  inputSource.start(frames);

  table_.clear();
  tablePtr_ = -1;
  sampleInCycle_ = 0;
  cycleCount_ = 0;
  inAdditive_ = false;
  additiveSample_ = 0;

  morphSamples_ = int(morphDuration.value * float(sample_rate()));
}

void HybridKSSource::fill_table() {
  int len = int(std::lround(float(sample_rate()) / currFreq_));
  if (len < 1) len = 1;
  table_.resize(len);

  for (int i = 0; i < len; ++i)
    table_[i] = inputSource.advance();

}

void HybridKSSource::ks_evolve(int index) {
  // Standard 2-sample KS averaging
  int len = int(table_.size());
  int next = (index + 1) % len;
  table_[index] = (table_[index] + table_[next]) * 0.5f;
}

void HybridKSSource::extract_partials() {
  // Per-harmonic DFT: extract amplitude and phase of each partial
  int N = int(table_.size());
  partialAmpl_.resize(numPartials.value);
  partialPhase_.resize(numPartials.value);
  startAmpl_.resize(numPartials.value);

  for (int k = 0; k < numPartials.value; ++k) {
    float re = 0.0f, im = 0.0f;
    int harmonic = k + 1;

    for (int n = 0; n < N; ++n) {
      float angle = TAU * float(harmonic) * float(n) / float(N);
      re += table_[n] * std::cos(angle);
      im -= table_[n] * std::sin(angle);
    }

    re *= 2.0f / float(N);
    im *= 2.0f / float(N);

    float ampl = std::sqrt(re * re + im * im);
    float phaseCycles = std::atan2(im, re) / TAU; // normalize to [0,1) cycles
    if (phaseCycles < 0.0f) phaseCycles += 1.0f;

    partialAmpl_[k] = ampl;
    partialPhase_[k] = phaseCycles;
    startAmpl_[k] = ampl;
  }

  // Advance phases to account for current position in the cycle
  // (the DFT extracted phases at table start; we're at tablePtr_ in the cycle)
  float cyclePos = float(tablePtr_) / float(N);
  for (int k = 0; k < numPartials.value; ++k) {
    partialPhase_[k] += cyclePos * float(k + 1);
    partialPhase_[k] -= std::floor(partialPhase_[k]);
  }
}

float HybridKSSource::compute_ks() {
  if (ptr_ == 0) {
    fill_table();
    tablePtr_ = -1;
    cycleCount_ = 0;
    sampleInCycle_ = 0;
  }

  int len = int(table_.size());
  tablePtr_ = (tablePtr_ + 1) % len;

  ks_evolve(tablePtr_);

  sampleInCycle_++;
  if (sampleInCycle_ >= len) {
    sampleInCycle_ = 0;
    cycleCount_++;

    // Check if hold phase is over
    if (cycleCount_ >= holdCycles.value) {
      // Debug: table energy before extraction
      float tblPeak = 0;
      for (float v : table_) if (std::fabs(v) > tblPeak) tblPeak = std::fabs(v);

      extract_partials();
      inAdditive_ = true;
      additiveSample_ = 0;

      float maxA = 0;
      for (int k = 0; k < numPartials.value; ++k)
        if (partialAmpl_[k] > maxA) maxA = partialAmpl_[k];
    }
  }

  return table_[tablePtr_];
}

float HybridKSSource::compute_additive() {
  // Interpolate partial amplitudes from start → target
  float t = (morphSamples_ > 0)
    ? std::min(float(additiveSample_) / float(morphSamples_), 1.0f)
    : 1.0f;

  float val = 0.0f;

  for (int k = 0; k < numPartials.value; ++k) {
    // Amplitude: interpolate from extracted KS amplitude toward target
    float ampl = startAmpl_[k] + (targetAmpl_[k] - startAmpl_[k]) * t;

    // Phase: advance naturally at harmonic frequency
    float freq = currFreq_ * float(k + 1);
    partialPhase_[k] += freq / float(sample_rate());
    partialPhase_[k] -= std::floor(partialPhase_[k]);

    val += std::sin(partialPhase_[k] * TAU) * ampl;
  }

  additiveSample_++;

  // Normalize similarly to other additive sources
  return val;
}

float HybridKSSource::wave() {
  if (!inAdditive_) {
    return compute_ks();
  } else {
    return compute_additive();
  }
}

} // namespace mforce
