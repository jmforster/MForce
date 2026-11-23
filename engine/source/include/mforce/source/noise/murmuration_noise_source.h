#pragma once
// Murmuration noise generator: flock-based emergent noise.
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <cmath>

namespace mforce {

// ---------------------------------------------------------------------------
// MurmurationNoiseSource — flock-based emergent noise.
//
// N oscillators ("birds") follow flocking rules. Their summed output is
// the signal. Interactions between voices create the texture:
//   - High cohesion: oscillators converge → tonal, buzzy
//   - Low cohesion: oscillators scatter → wide, rich noise
//   - Perturbations cascade through neighbors → organic spectral sweeps
//   - Separation prevents unison collapse → natural beating/chorus
//
// All parameters are modulatable (connectable pins).
// ---------------------------------------------------------------------------
struct MurmurationNoiseSource final : Generator {
  static constexpr int MAX_BIRDS = 64;

  Param count     {this, "count",      16.0f,  2.0f,  64.0f};
  Param cohesion  {this, "cohesion",   0.3f,   0.0f,  1.0f};
  Param alignment {this, "alignment",  0.5f,   0.0f,  1.0f};
  Param separation{this, "separation", 0.02f,  0.0f,  0.2f};
  Param chaos     {this, "chaos",      0.1f,   0.0f,  1.0f};
  Param speed     {this, "speed",      440.0f, 0.01f, 20000.0f};

  explicit MurmurationNoiseSource(uint32_t seed = 0xB18D'0001u) : rng_(seed) {}

  // No unit range: the flock sum is scaled by 1/sqrt(n), so it exceeds 1.
  const char* type_name() const override { return "MurmurationNoiseSource"; }

  void on_start(int) override {
    // Initialize birds with scattered frequencies around center
    for (int i = 0; i < MAX_BIRDS; ++i) {
      birds_[i].phase = rng_.value();              // random starting phase
      birds_[i].freqOffset = rng_.valuePN() * 0.5f; // scattered ±50% around center
      birds_[i].velocity = 0.0f;                    // no initial drift
    }
  }

  float compute() override {
    const int n = std::clamp(int(count.current()), 2, MAX_BIRDS);
    const float coh = cohesion.current();
    const float ali = alignment.current();
    const float sep = separation.current();
    const float cha = chaos.current();
    const float centerFreq = speed.current();

    // --- Flocking update (runs at audio rate, scaled by frequency) ---
    float dt = centerFreq / float(sample_rate());  // scale forces by base period

    // Compute flock center and average velocity
    float avgOffset = 0.0f;
    float avgVelocity = 0.0f;
    for (int i = 0; i < n; ++i) {
      avgOffset += birds_[i].freqOffset;
      avgVelocity += birds_[i].velocity;
    }
    avgOffset /= float(n);
    avgVelocity /= float(n);

    // Apply forces to each bird
    for (int i = 0; i < n; ++i) {
      auto& b = birds_[i];

      // Cohesion: pull toward flock center frequency
      float cohForce = (avgOffset - b.freqOffset) * coh * 0.1f;

      // Alignment: match average velocity (smooth the flock)
      float aliForce = (avgVelocity - b.velocity) * ali * 0.1f;

      // Separation: push away from nearest neighbor
      float sepForce = 0.0f;
      float minDist = 1e9f;
      for (int j = 0; j < n; ++j) {
        if (j == i) continue;
        float dist = b.freqOffset - birds_[j].freqOffset;
        float absDist = std::fabs(dist);
        if (absDist < minDist) {
          minDist = absDist;
          if (absDist < sep && absDist > 0.0001f) {
            // Push away, force inversely proportional to distance
            sepForce += (dist / absDist) * sep * 0.5f / (absDist + 0.001f);
          }
        }
      }

      // Chaos: random perturbation
      float chaForce = rng_.valuePN() * cha * 0.05f;

      // Update velocity (with damping to prevent runaway)
      b.velocity += (cohForce + aliForce + sepForce + chaForce) * dt;
      b.velocity *= 0.995f;  // damping

      // Clamp velocity to prevent extreme jumps
      b.velocity = std::clamp(b.velocity, -0.5f, 0.5f);

      // Update frequency offset
      b.freqOffset += b.velocity * dt;

      // Soft boundary: keep birds within ±2 octaves of center
      if (std::fabs(b.freqOffset) > 2.0f) {
        b.freqOffset *= 0.99f;
        b.velocity *= -0.5f;
      }
    }

    // --- Generate audio: sum all bird oscillators ---
    float sum = 0.0f;
    for (int i = 0; i < n; ++i) {
      auto& b = birds_[i];

      // Bird frequency = center * 2^offset (offset in octaves)
      float freq = centerFreq * std::pow(2.0f, b.freqOffset);
      float phaseInc = freq / float(sample_rate());

      b.phase += phaseInc;
      if (b.phase >= 1.0f) b.phase -= 1.0f;

      // Simple sine oscillator per bird
      sum += std::sin(b.phase * 6.283185307f);
    }

    // Normalize by sqrt(n) for consistent amplitude across flock sizes
    // (sqrt because uncorrelated signals add in quadrature)
    return sum / std::sqrt(float(n));
  }

private:
  struct Bird {
    float phase{0.0f};       // oscillator phase [0,1]
    float freqOffset{0.0f};  // offset from center in octaves
    float velocity{0.0f};    // rate of frequency drift
  };

  // Per-note determinism (onsets-v2 addendum): re-anchor draws.
  void reseed() override { rng_.reanchor(); }
  Randomizer rng_;
  Bird birds_[MAX_BIRDS];
};

} // namespace mforce
