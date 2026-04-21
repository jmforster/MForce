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
// No modulatable params — spectral shape is fixed by definition.
// ---------------------------------------------------------------------------
struct BlueNoiseSource final : ValueSource {
  explicit BlueNoiseSource(uint32_t seed = 0xB100'0001u)
  : pink_(PinkNoiseSource::DEFAULT_ROWS, seed) {}

  const char* type_name() const override { return "BlueNoiseSource"; }
  SourceCategory category() const override { return SourceCategory::Generator; }

  void prepare(const RenderContext& ctx, int frames) override {
    pink_.prepare(ctx, frames);
    prev_ = 0.0f;
  }

  float next() override {
    float p = pink_.next();
    cur_ = p - prev_;
    prev_ = p;
    return cur_;
  }

  float current() const override { return cur_; }

private:
  PinkNoiseSource pink_;
  float prev_{0.0f};
  float cur_{0.0f};
};

// ---------------------------------------------------------------------------
// VioletNoiseSource — +6dB/octave (spectral inverse of brown/red).
// Differentiated white noise: output = current_white - previous_white.
// No modulatable params — spectral shape is fixed by definition.
// ---------------------------------------------------------------------------
struct VioletNoiseSource final : ValueSource {
  explicit VioletNoiseSource(uint32_t seed = 0xF100'0001u)
  : rng_(seed) {}

  const char* type_name() const override { return "VioletNoiseSource"; }
  SourceCategory category() const override { return SourceCategory::Generator; }

  void prepare(const RenderContext& ctx, int frames) override { prev_ = 0.0f; }

  float next() override {
    float w = rng_.valuePN();
    cur_ = w - prev_;
    prev_ = w;
    cur_ *= 0.5f;
    return cur_;
  }

  float current() const override { return cur_; }

private:
  Randomizer rng_;
  float prev_{0.0f};
  float cur_{0.0f};
};

// ---------------------------------------------------------------------------
// VelvetNoiseSource — sparse random impulse sequence.
// Only occasional samples are non-zero (±1), rest are zero.
// Useful as KS excitation or for reverb-like textures.
// ---------------------------------------------------------------------------
struct VelvetNoiseSource final : ValueSource {
  explicit VelvetNoiseSource(int sampleRate = 48000, uint32_t seed = 0xFE17'0001u)
  : sampleRate_(sampleRate), rng_(seed),
    density_(std::make_shared<ConstantSource>(2000.0f)),
    amplitude_(std::make_shared<ConstantSource>(1.0f)) {}

  const char* type_name() const override { return "VelvetNoiseSource"; }
  SourceCategory category() const override { return SourceCategory::Generator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"density",   2000.0f, 1.0f, 48000.0f},
      {"amplitude", 1.0f,    0.0f, 10.0f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "density")   density_ = std::move(src);
    if (name == "amplitude") amplitude_ = std::move(src);
  }
  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "density")   return density_;
    if (name == "amplitude") return amplitude_;
    return nullptr;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    counter_ = 0;
    density_->prepare(ctx, frames);
    amplitude_->prepare(ctx, frames);
    recompute_gap();
  }

  float next() override {
    float d = density_->next();
    float a = amplitude_->next();
    cur_ = 0.0f;
    if (counter_ >= nextImpulse_) {
      cur_ = (rng_.value() < 0.5f ? a : -a);
      counter_ = 0;
      // Recompute gap from current density
      float avgGap = (d > 0.0f) ? float(sampleRate_) / d : float(sampleRate_);
      nextImpulse_ = std::max(1, int(avgGap * (0.5f + rng_.value())));
    }
    counter_++;
    return cur_;
  }

  float current() const override { return cur_; }

private:
  void recompute_gap() {
    nextImpulse_ = std::max(1, sampleRate_ / 2000);
  }

  int sampleRate_;
  Randomizer rng_;
  std::shared_ptr<ValueSource> density_;
  std::shared_ptr<ValueSource> amplitude_;
  int counter_{0};
  int nextImpulse_{0};
  float cur_{0.0f};
};

// ---------------------------------------------------------------------------
// PerlinNoiseSource — coherent gradient noise at audio rate.
// Produces smoothly varying noise with controllable texture.
// At low speed = organic modulation source.
// At high speed = noise with controllable smoothness.
// ---------------------------------------------------------------------------
struct PerlinNoiseSource final : ValueSource {
  explicit PerlinNoiseSource(int sampleRate = 48000, uint32_t seed = 0xAE21'0001u)
  : sampleRate_(sampleRate), rng_(seed),
    speed_(std::make_shared<ConstantSource>(440.0f)),
    octaves_(std::make_shared<ConstantSource>(4.0f)),
    persistence_(std::make_shared<ConstantSource>(0.5f)),
    lacunarity_(std::make_shared<ConstantSource>(2.0f)) {
    for (int i = 0; i < 512; ++i) perm_[i] = int(rng_.value() * 256.0f) & 255;
  }

  const char* type_name() const override { return "PerlinNoiseSource"; }
  SourceCategory category() const override { return SourceCategory::Generator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"speed",       440.0f, 0.01f, 20000.0f},
      {"octaves",     4.0f,   1.0f,  8.0f},
      {"persistence", 0.5f,   0.0f,  1.0f},
      {"lacunarity",  2.0f,   1.0f,  4.0f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "speed")       speed_ = std::move(src);
    if (name == "octaves")     octaves_ = std::move(src);
    if (name == "persistence") persistence_ = std::move(src);
    if (name == "lacunarity")  lacunarity_ = std::move(src);
  }
  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "speed")       return speed_;
    if (name == "octaves")     return octaves_;
    if (name == "persistence") return persistence_;
    if (name == "lacunarity")  return lacunarity_;
    return nullptr;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    pos_ = 0.0;
    speed_->prepare(ctx, frames);
    octaves_->prepare(ctx, frames);
    persistence_->prepare(ctx, frames);
    lacunarity_->prepare(ctx, frames);
  }

  float next() override {
    float spd = speed_->next();
    int oct = std::clamp(int(octaves_->next()), 1, 8);
    float pers = persistence_->next();
    float lac = lacunarity_->next();

    pos_ += double(spd) / double(sampleRate_);

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

    cur_ = (maxAmp > 0.0f) ? total / maxAmp : 0.0f;
    return cur_;
  }

  float current() const override { return cur_; }

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

  int sampleRate_;
  Randomizer rng_;
  int perm_[512];
  double pos_{0.0};
  std::shared_ptr<ValueSource> speed_;
  std::shared_ptr<ValueSource> octaves_;
  std::shared_ptr<ValueSource> persistence_;
  std::shared_ptr<ValueSource> lacunarity_;
  float cur_{0.0f};
};

// ---------------------------------------------------------------------------
// CrackleNoiseSource — chaotic map producing vinyl-crackle-like sounds.
// y[n] = |chaos * y[n-1] - y[n-2] - 0.05|
//   chaos near 1.0 = gentle crackle
//   chaos near 1.5 = aggressive crackle
//   chaos near 2.0 = chaotic noise
// ---------------------------------------------------------------------------
struct CrackleNoiseSource final : ValueSource {
  explicit CrackleNoiseSource(uint32_t seed = 0xC8AC'0001u)
  : rng_(seed),
    chaos_(std::make_shared<ConstantSource>(1.5f)) {}

  const char* type_name() const override { return "CrackleNoiseSource"; }
  SourceCategory category() const override { return SourceCategory::Generator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"chaos", 1.5f, 1.0f, 2.0f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "chaos") chaos_ = std::move(src);
  }
  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "chaos") return chaos_;
    return nullptr;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    y1_ = rng_.value() * 0.1f;
    y2_ = rng_.value() * 0.1f;
    chaos_->prepare(ctx, frames);
  }

  float next() override {
    float c = chaos_->next();
    float y0 = std::fabs(c * y1_ - y2_ - 0.05f);
    y0 = std::clamp(y0, -1.0f, 1.0f);
    y2_ = y1_;
    y1_ = y0;
    cur_ = y0 * 2.0f - 1.0f;
    return cur_;
  }

  float current() const override { return cur_; }

private:
  Randomizer rng_;
  std::shared_ptr<ValueSource> chaos_;
  float y1_{0.0f};
  float y2_{0.0f};
  float cur_{0.0f};
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
struct MurmurationNoiseSource final : ValueSource {
  static constexpr int MAX_BIRDS = 64;

  explicit MurmurationNoiseSource(int sampleRate = 48000, uint32_t seed = 0xB18D'0001u)
  : sampleRate_(sampleRate), rng_(seed),
    count_(std::make_shared<ConstantSource>(16.0f)),
    cohesion_(std::make_shared<ConstantSource>(0.3f)),
    alignment_(std::make_shared<ConstantSource>(0.5f)),
    separation_(std::make_shared<ConstantSource>(0.02f)),
    chaos_(std::make_shared<ConstantSource>(0.1f)),
    speed_(std::make_shared<ConstantSource>(440.0f)) {}

  const char* type_name() const override { return "MurmurationNoiseSource"; }
  SourceCategory category() const override { return SourceCategory::Generator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"count",      16.0f, 2.0f,  64.0f},
      {"cohesion",   0.3f,  0.0f,  1.0f},
      {"alignment",  0.5f,  0.0f,  1.0f},
      {"separation", 0.02f, 0.0f,  0.2f},
      {"chaos",      0.1f,  0.0f,  1.0f},
      {"speed",      440.0f, 0.01f, 20000.0f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "count")      count_ = std::move(src);
    if (name == "cohesion")   cohesion_ = std::move(src);
    if (name == "alignment")  alignment_ = std::move(src);
    if (name == "separation") separation_ = std::move(src);
    if (name == "chaos")      chaos_ = std::move(src);
    if (name == "speed")      speed_ = std::move(src);
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "count")      return count_;
    if (name == "cohesion")   return cohesion_;
    if (name == "alignment")  return alignment_;
    if (name == "separation") return separation_;
    if (name == "chaos")      return chaos_;
    if (name == "speed")      return speed_;
    return nullptr;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    count_->prepare(ctx, frames);
    cohesion_->prepare(ctx, frames);
    alignment_->prepare(ctx, frames);
    separation_->prepare(ctx, frames);
    chaos_->prepare(ctx, frames);
    speed_->prepare(ctx, frames);

    // Initialize birds with scattered frequencies around center
    for (int i = 0; i < MAX_BIRDS; ++i) {
      birds_[i].phase = rng_.value();              // random starting phase
      birds_[i].freqOffset = rng_.valuePN() * 0.5f; // scattered ±50% around center
      birds_[i].velocity = 0.0f;                    // no initial drift
    }
  }

  float next() override {
    int n = std::clamp(int(count_->next()), 2, MAX_BIRDS);
    float coh = cohesion_->next();
    float ali = alignment_->next();
    float sep = separation_->next();
    float cha = chaos_->next();
    float centerFreq = speed_->next();

    // --- Flocking update (runs at audio rate, scaled by frequency) ---
    float dt = centerFreq / float(sampleRate_);  // scale forces by base period

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
      float phaseInc = freq / float(sampleRate_);

      b.phase += phaseInc;
      if (b.phase >= 1.0f) b.phase -= 1.0f;

      // Simple sine oscillator per bird
      sum += std::sin(b.phase * 6.283185307f);
    }

    // Normalize by sqrt(n) for consistent amplitude across flock sizes
    // (sqrt because uncorrelated signals add in quadrature)
    cur_ = sum / std::sqrt(float(n));

    return cur_;
  }

  float current() const override { return cur_; }

private:
  struct Bird {
    float phase{0.0f};       // oscillator phase [0,1]
    float freqOffset{0.0f};  // offset from center in octaves
    float velocity{0.0f};    // rate of frequency drift
  };

  int sampleRate_;
  Randomizer rng_;
  Bird birds_[MAX_BIRDS];

  std::shared_ptr<ValueSource> count_;
  std::shared_ptr<ValueSource> cohesion_;
  std::shared_ptr<ValueSource> alignment_;
  std::shared_ptr<ValueSource> separation_;
  std::shared_ptr<ValueSource> chaos_;
  std::shared_ptr<ValueSource> speed_;
  float cur_{0.0f};
};

} // namespace mforce
