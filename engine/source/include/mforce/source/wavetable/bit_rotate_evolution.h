#pragma once
// BitRotateEvolution: a WaveEvolution.
#include "mforce/source/wavetable/wave_evolution.h"
#include <algorithm>
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// BitRotateEvolution — bit-level glitch transform. Each sample is mapped
// to a 32-bit unsigned int via fixed-point, its bits rotated by `shiftBits`,
// then mapped back. `stepEvery` controls how often a sample is rotated (so
// only a fraction of the table mutates per cycle). Endpoint: pseudo-random
// scramble of the original — aliasing-laden, hard-edged.
// ---------------------------------------------------------------------------
struct BitRotateEvolution final : WaveEvolution {
  // The two pins are advanced here every evolve() call (= once per output
  // sample, stepEvery first) so a wired envelope advances on the note's
  // sample clock even between rotations; they are Manual.
  Param shiftBits{this, "shiftBits", 3.0f, 0.0f, 31.0f,  "int", Pull::Manual};   // rotation depth, int 0..31
  Param stepEvery{this, "stepEvery", 4.0f, 1.0f, 256.0f, "int", Pull::Manual};   // samples between rotations, int >= 1

  const char* type_name() const override { return "BitRotateEvolution"; }

  void adjust(float /*frequency*/) override {
    ctr_ = 0;
  }

  void evolve(std::vector<float>& values, int index) override {
    const int step = std::max(1, int(stepEvery.next()));
    const int bits = int(shiftBits.next());
    ctr_++;
    if (ctr_ < step) return;
    ctr_ = 0;

    const int k = ((bits % 32) + 32) % 32;
    if (k == 0) return;

    const float v = std::clamp(values[index], -1.0f, 1.0f);
    const uint32_t u = uint32_t((double(v) + 1.0) * 0.5 * 4294967295.0);
    const uint32_t rot = (u >> k) | (u << (32 - k));
    values[index] = float(double(rot) / 4294967295.0 * 2.0 - 1.0);
  }

private:
  int ctr_{0};
};

} // namespace mforce
