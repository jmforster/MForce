#pragma once
// CellularAutomatonEvolution: a WaveEvolution.
#include "mforce/source/wavetable/wave_evolution.h"
#include <algorithm>
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// CellularAutomatonEvolution — binary 1D Wolfram CA on the wavetable.
// The wavetable is threshold-quantized to bits each cycle, and a Wolfram
// rule (0..255) is applied to each cell from its three-cell neighbourhood.
// Bits flip back to ± waveform via `levelHi` / `levelLo`. Famous rules:
// 30 (chaotic), 90 (Sierpinski), 110 (edge-of-chaos), 184 (traffic).
// ---------------------------------------------------------------------------
struct CellularAutomatonEvolution final : WaveEvolution {
  Setting<int>   rule     {this, "rule",      110,   0,     255};
  Setting<float> threshold{this, "threshold", 0.0f,  -1.0f, 1.0f};   // discretization threshold (sample > threshold → 1)
  Setting<float> levelHi  {this, "levelHi",   0.8f,  0.0f,  1.0f};
  Setting<float> levelLo  {this, "levelLo",   -0.8f, -1.0f, 0.0f};
  Setting<int>   stepEvery{this, "stepEvery", 8,     1,     256};    // do one CA step every Nth sample (slow tempo)

  const char* type_name() const override { return "CellularAutomatonEvolution"; }

  void on_setting_changed(std::string_view name) override {
    if (name == "rule")           rule.value = std::clamp(rule.value, 0, 255);
    else if (name == "stepEvery") stepEvery.value = std::max(1, stepEvery.value);
  }

  void on_prepare(const RenderContext&, int maxLen) override {
    state_.reserve(size_t(maxLen));
    snapshot_.reserve(size_t(maxLen));
  }
  void adjust(float /*frequency*/) override {
    sampleCtr_ = 0;
  }

  void shape_excitation(std::vector<float>& values) override {
    const int len = int(values.size());
    state_.assign(len, 0);
    snapshot_.assign(len, 0);
    for (int i = 0; i < len; ++i) state_[i] = (values[i] > threshold.value) ? 1 : 0;
  }

  void evolve(std::vector<float>& values, int index) override {
    const int len = int(values.size());
    if (len < 3) return;

    if (state_.size() != size_t(len)) {
      state_.assign(len, 0);
      snapshot_.assign(len, 0);
      for (int i = 0; i < len; ++i) state_[i] = (values[i] > threshold.value) ? 1 : 0;
    }

    // Snapshot at cycle boundary so neighbours are read from the
    // previous cycle's complete state, not from cells already mutated
    // earlier in this cycle. Without this, asynchronous-CA dynamics
    // collapse to uniform output very fast.
    if (index == 0) snapshot_ = state_;

    // Tempo control: only do CA work every Nth sample.
    sampleCtr_++;
    if (sampleCtr_ >= stepEvery.value) {
      sampleCtr_ = 0;
      const int im = (index - 1 + len) % len;
      const int ip = (index + 1) % len;
      const uint8_t pat = uint8_t((snapshot_[im] << 2) | (snapshot_[index] << 1) | snapshot_[ip]);
      state_[index] = uint8_t((rule.value >> pat) & 1u);
    }
    values[index] = state_[index] ? levelHi.value : levelLo.value;
  }

private:
  std::vector<uint8_t> state_;
  std::vector<uint8_t> snapshot_;
  int sampleCtr_{0};
};

} // namespace mforce
