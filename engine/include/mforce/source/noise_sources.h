#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/source/pink_noise_source.h"
#include "mforce/source/white_noise_source.h"
#include "mforce/core/randomizer.h"
#include <cmath>
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// BlueNoiseSource — +3dB/octave (spectral inverse of pink).
// Differentiated pink noise: output = current_pink - previous_pink.
// Spectral SHAPE is fixed by definition; only level is modulatable.
// ---------------------------------------------------------------------------
struct BlueNoiseSource final : Generator {
  Param amplitude{this, "amplitude", 1.0f, 0.0f, 10.0f};

  explicit BlueNoiseSource(uint32_t seed = 0xB100'0001u)
  : pink_(PinkNoiseSource::DEFAULT_ROWS, seed) {}

  // No unit range: the difference of two pink samples can reach +-2.
  const char* type_name() const override { return "BlueNoiseSource"; }
  SourceCategory category() const override { return SourceCategory::Generator; }

  void on_prepare(const RenderContext& ctx) override { pink_.prepare(ctx); }

  void on_start(int frames) override {
    pink_.start(frames);
    prev_ = 0.0f;
  }

  float compute() override {
    const float p = pink_.next();
    const float out = (p - prev_) * amplitude.current();
    prev_ = p;
    return out;
  }

private:
  PinkNoiseSource pink_;
  float prev_{0.0f};
};

// ---------------------------------------------------------------------------
// VioletNoiseSource — +6dB/octave (spectral inverse of brown/red).
// Differentiated white noise: output = current_white - previous_white.
// Spectral SHAPE is fixed by definition; only level is modulatable.
// ---------------------------------------------------------------------------
struct VioletNoiseSource final : Generator {
  Param amplitude{this, "amplitude", 1.0f, 0.0f, 10.0f};

  explicit VioletNoiseSource(uint32_t seed = 0xF100'0001u) : rng_(seed) {}

  Range range() const override { return Range::Bipolar; }
  const char* type_name() const override { return "VioletNoiseSource"; }
  SourceCategory category() const override { return SourceCategory::Generator; }

  void on_start(int) override { prev_ = 0.0f; }

  float compute() override {
    const float w = rng_.valuePN();
    float out = w - prev_;
    prev_ = w;
    out *= 0.5f * amplitude.current();
    return out;
  }

private:
  // Per-note determinism (onsets-v2 addendum): re-anchor draws.
  void reseed() override { rng_.reanchor(); }
  Randomizer rng_;
  float prev_{0.0f};
};

// ---------------------------------------------------------------------------
// VelvetNoiseSource — sparse random impulse sequence.
// Only occasional samples are non-zero (±1), rest are zero.
// Useful as KS excitation or for reverb-like textures.
// ---------------------------------------------------------------------------
struct VelvetNoiseSource final : Generator {
  Param density  {this, "density",   2000.0f, 1.0f, 48000.0f};
  Param amplitude{this, "amplitude", 1.0f,    0.0f, 10.0f};

  explicit VelvetNoiseSource(uint32_t seed = 0xFE17'0001u) : rng_(seed) {}

  Range range() const override { return Range::Bipolar; }
  const char* type_name() const override { return "VelvetNoiseSource"; }
  SourceCategory category() const override { return SourceCategory::Generator; }

  void on_start(int) override {
    counter_ = 0;
    recompute_gap();
  }

  float compute() override {
    const float d = density.current();
    const float a = amplitude.current();
    float out = 0.0f;
    if (counter_ >= nextImpulse_) {
      out = (rng_.value() < 0.5f ? a : -a);
      counter_ = 0;
      // Recompute gap from current density
      float avgGap = (d > 0.0f) ? float(sample_rate()) / d : float(sample_rate());
      nextImpulse_ = std::max(1, int(avgGap * (0.5f + rng_.value())));
    }
    counter_++;
    return out;
  }

private:
  void recompute_gap() {
    nextImpulse_ = std::max(1, sample_rate() / 2000);
  }

  // Per-note determinism (onsets-v2 addendum): re-anchor draws.
  void reseed() override { rng_.reanchor(); }
  Randomizer rng_;
  int counter_{0};
  int nextImpulse_{0};
};

// ---------------------------------------------------------------------------
// PerlinNoiseSource — coherent gradient noise at audio rate.
// Produces smoothly varying noise with controllable texture.
// At low speed = organic modulation source.
// At high speed = noise with controllable smoothness.
// ---------------------------------------------------------------------------
struct PerlinNoiseSource final : Generator {
  Param speed      {this, "speed",       440.0f, 0.01f, 20000.0f};
  Param octaves    {this, "octaves",     4.0f,   1.0f,  8.0f};
  Param persistence{this, "persistence", 0.5f,   0.0f,  1.0f};
  Param lacunarity {this, "lacunarity",  2.0f,   1.0f,  4.0f};

  explicit PerlinNoiseSource(uint32_t seed = 0xAE21'0001u) : rng_(seed) {
    for (int i = 0; i < 512; ++i) perm_[i] = int(rng_.value() * 256.0f) & 255;
  }

  Range range() const override { return Range::Bipolar; }
  const char* type_name() const override { return "PerlinNoiseSource"; }
  SourceCategory category() const override { return SourceCategory::Generator; }

  void on_start(int) override { pos_ = 0.0; }

  float compute() override {
    const float spd = speed.current();
    const int oct = std::clamp(int(octaves.current()), 1, 8);
    const float pers = persistence.current();
    const float lac = lacunarity.current();

    pos_ += double(spd) / double(sample_rate());

    float total = 0.0f;
    float amplitude = 1.0f;
    float maxAmp = 0.0f;
    double freq = 1.0;

    for (int i = 0; i < oct; ++i) {
      total += amplitude * noise1d(float(pos_ * freq));
      maxAmp += amplitude;
      amplitude *= pers;
      freq *= double(lac);
    }

    return (maxAmp > 0.0f) ? total / maxAmp : 0.0f;
  }

private:
  float noise1d(float x) const {
    int xi = int(std::floor(x)) & 255;
    float xf = x - std::floor(x);
    float u = fade(xf);
    float g0 = grad1d(perm_[xi], xf);
    float g1 = grad1d(perm_[xi + 1], xf - 1.0f);
    return lerp(g0, g1, u);
  }

  static float fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
  }
  static float lerp(float a, float b, float t) { return a + t * (b - a); }
  static float grad1d(int hash, float x) { return (hash & 1) ? x : -x; }

  // Per-note determinism (onsets-v2 addendum): re-anchor draws.
  void reseed() override { rng_.reanchor(); }
  Randomizer rng_;
  int perm_[512];
  double pos_{0.0};
};

// ---------------------------------------------------------------------------
// CrackleNoiseSource — chaotic map producing vinyl-crackle-like sounds.
// y[n] = |chaos * y[n-1] - y[n-2] - 0.05|
//   chaos near 1.0 = gentle crackle
//   chaos near 1.5 = aggressive crackle
//   chaos near 2.0 = chaotic noise
// ---------------------------------------------------------------------------
struct CrackleNoiseSource final : Generator {
  Param chaos{this, "chaos", 1.5f, 1.0f, 2.0f};

  explicit CrackleNoiseSource(uint32_t seed = 0xC8AC'0001u) : rng_(seed) {}

  Range range() const override { return Range::Bipolar; }
  const char* type_name() const override { return "CrackleNoiseSource"; }
  SourceCategory category() const override { return SourceCategory::Generator; }

  void on_start(int) override {
    y1_ = rng_.value() * 0.1f;
    y2_ = rng_.value() * 0.1f;
  }

  float compute() override {
    const float c = chaos.current();
    float y0 = std::fabs(c * y1_ - y2_ - 0.05f);
    y0 = std::clamp(y0, -1.0f, 1.0f);
    y2_ = y1_;
    y1_ = y0;
    return y0 * 2.0f - 1.0f;
  }

private:
  // Per-note determinism (onsets-v2 addendum): re-anchor draws.
  void reseed() override { rng_.reanchor(); }
  Randomizer rng_;
  float y1_{0.0f};
  float y2_{0.0f};
};

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
  SourceCategory category() const override { return SourceCategory::Generator; }

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
