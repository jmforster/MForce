#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

namespace mforce {

// ---------------------------------------------------------------------------
// SelfAvoidingWalkSource — a walker on a 2D grid that never revisits a cell.
//
// The walker moves one step per integration step, choosing from the set of
// unvisited orthogonal neighbours. When it gets trapped (all four neighbours
// are out-of-bounds or already visited), a "trap click" is emitted and the
// walk restarts from a fresh seed cell on a cleared grid.
//
// `rate` controls walker steps per second (integration rate). Output is
// derived from the walker's position in a few possible modes. Small direction
// dither and a clickable reset impulse give it audio-rate texture.
// ---------------------------------------------------------------------------
struct SelfAvoidingWalkSource final : ValueSource {
  static constexpr int kMaxN = 64;

  int gridSize{32};
  int outputMode{0};    // 0 = x, 1 = y, 2 = (x+y) diagonal, 3 = manhattan
  int startMode{0};     // 0 = center, 1 = random, 2 = corner

  SelfAvoidingWalkSource(int sampleRate, uint32_t seed = 0x5AA70001u)
  : sampleRate_(sampleRate), rng_(seed),
    amplitude_(std::make_shared<ConstantSource>(0.5f)),
    rate_(std::make_shared<ConstantSource>(48000.0f)),
    directionBias_(std::make_shared<ConstantSource>(0.0f)),
    clickAmount_(std::make_shared<ConstantSource>(0.5f)),
    ditherAmount_(std::make_shared<ConstantSource>(0.05f)),
    smoothness_(std::make_shared<ConstantSource>(0.0f))
  {
    reset_walk();
  }

  const char* type_name() const override { return "SelfAvoidingWalkSource"; }
  SourceCategory category() const override { return SourceCategory::Oscillator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"amplitude",     0.5f,     0.0f,   10.0f},
      {"rate",          48000.0f, 0.0f,   2000000.0f},
      {"directionBias", 0.0f,    -1.0f,   1.0f},
      {"clickAmount",   0.5f,     0.0f,   4.0f},
      {"ditherAmount",  0.05f,    0.0f,   1.0f},
      {"smoothness",    0.0f,     0.0f,   0.999f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "amplitude")     { amplitude_     = std::move(src); return; }
    if (name == "rate")          { rate_          = std::move(src); return; }
    if (name == "directionBias") { directionBias_ = std::move(src); return; }
    if (name == "clickAmount")   { clickAmount_   = std::move(src); return; }
    if (name == "ditherAmount")  { ditherAmount_  = std::move(src); return; }
    if (name == "smoothness")    { smoothness_    = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "amplitude")     return amplitude_;
    if (name == "rate")          return rate_;
    if (name == "directionBias") return directionBias_;
    if (name == "clickAmount")   return clickAmount_;
    if (name == "ditherAmount")  return ditherAmount_;
    if (name == "smoothness")    return smoothness_;
    return nullptr;
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"gridSize",   ConfigType::Int, 32.0f, 4.0f, 64.0f},
      {"outputMode", ConfigType::Int, 0.0f,  0.0f, 3.0f},
      {"startMode",  ConfigType::Int, 0.0f,  0.0f, 2.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "gridSize") {
      int n = std::clamp(int(value), 4, kMaxN);
      if (n != gridSize) { gridSize = n; reset_walk(); }
      return;
    }
    if (name == "outputMode") { outputMode = std::clamp(int(value), 0, 3); return; }
    if (name == "startMode")  { startMode  = std::clamp(int(value), 0, 2); reset_walk(); return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "gridSize")   return float(gridSize);
    if (name == "outputMode") return float(outputMode);
    if (name == "startMode")  return float(startMode);
    return 0.0f;
  }

  void prepare(int frames) override {
    if (amplitude_)     amplitude_->prepare(frames);
    if (rate_)          rate_->prepare(frames);
    if (directionBias_) directionBias_->prepare(frames);
    if (clickAmount_)   clickAmount_->prepare(frames);
    if (ditherAmount_)  ditherAmount_->prepare(frames);
    if (smoothness_)    smoothness_->prepare(frames);
  }

  float next() override {
    amplitude_->next();
    rate_->next();
    directionBias_->next();
    clickAmount_->next();
    ditherAmount_->next();
    smoothness_->next();

    float rateHz = std::max(0.0f, rate_->current());
    opCredits_ += rateHz / float(sampleRate_);

    while (opCredits_ >= 1.0f) {
      step_once();
      opCredits_ -= 1.0f;
    }

    const int N = gridSize;
    const float denom = float(std::max(1, N - 1));

    float raw = 0.0f;
    switch (outputMode) {
      case 0: raw = (float(x_) / denom) * 2.0f - 1.0f; break;
      case 1: raw = (float(y_) / denom) * 2.0f - 1.0f; break;
      case 2: raw = (float(x_ + y_) / float(std::max(1, 2 * (N - 1)))) * 2.0f - 1.0f; break;
      case 3: {
        float md = float(std::abs(x_) + std::abs(y_));
        raw = (md / float(std::max(1, 2 * (N - 1)))) * 2.0f - 1.0f;
        break;
      }
    }

    // Direction-derived LSB dither (tiny).
    float dither = std::clamp(ditherAmount_->current(), 0.0f, 1.0f);
    float dirNibble = float(lastDir_ & 0x3) * 0.25f; // 0, .25, .5, .75
    raw += rng_.valuePN() * 0.02f * dither * (0.5f + dirNibble);

    if (clickPending_) {
      raw += std::clamp(clickAmount_->current(), 0.0f, 8.0f) * rng_.valuePN();
      clickPending_ = false;
    }

    float s = std::clamp(smoothness_->current(), 0.0f, 0.999f);
    float smoothed = (1.0f - s) * raw + s * smoothedPrev_;
    smoothedPrev_ = smoothed;

    cur_ = smoothed * amplitude_->current();
    return cur_;
  }

  float current() const override { return cur_; }

private:
  // Directions: 0=up (-y), 1=down (+y), 2=left (-x), 3=right (+x).
  static constexpr int kDx[4] = { 0,  0, -1, +1 };
  static constexpr int kDy[4] = {-1, +1,  0,  0 };

  void step_once() {
    const int N = gridSize;

    // Collect unvisited, in-bounds neighbours.
    int candDir[4];
    int candCount = 0;
    for (int d = 0; d < 4; ++d) {
      int nx = x_ + kDx[d];
      int ny = y_ + kDy[d];
      if (nx < 0 || nx >= N || ny < 0 || ny >= N) continue;
      if (visited_[nx][ny] != 0) continue;
      candDir[candCount++] = d;
    }

    if (candCount == 0) {
      // Trapped: emit click, restart walk from a fresh random seed cell.
      clickPending_ = true;
      ++resetCounter_;
      reset_walk_pick_random();
      return;
    }

    float bias = std::clamp(directionBias_->current(), -1.0f, 1.0f);

    // Weighted pick: positive bias favours right (3) and down (1);
    // negative bias favours left (2) and up (0). Zero bias is uniform.
    float weights[4];
    float total = 0.0f;
    for (int i = 0; i < candCount; ++i) {
      int d = candDir[i];
      float w = 1.0f;
      if (bias > 0.0f) {
        if (d == 1 || d == 3) w = 1.0f + 3.0f * bias;
        else                  w = std::max(0.01f, 1.0f - bias);
      } else if (bias < 0.0f) {
        float ab = -bias;
        if (d == 0 || d == 2) w = 1.0f + 3.0f * ab;
        else                  w = std::max(0.01f, 1.0f - ab);
      }
      weights[i] = w;
      total += w;
    }

    float r = rng_.value() * total;
    int pickIdx = candCount - 1;
    float acc = 0.0f;
    for (int i = 0; i < candCount; ++i) {
      acc += weights[i];
      if (r <= acc) { pickIdx = i; break; }
    }
    int d = candDir[pickIdx];

    int nx = x_ + kDx[d];
    int ny = y_ + kDy[d];

    // Safety bounds-check (defensive; candidate list already validated).
    if (nx < 0 || nx >= N || ny < 0 || ny >= N) {
      clickPending_ = true;
      reset_walk_pick_random();
      return;
    }

    x_ = nx;
    y_ = ny;
    ++stepCount_;
    // Age stored as step-count+1; saturate at 255 for rainbow colouring later.
    int age = std::min(stepCount_ + 1, 255);
    visited_[x_][y_] = uint8_t(age);
    lastDir_ = d;
  }

  void clear_visited() {
    for (int i = 0; i < kMaxN; ++i) {
      for (int j = 0; j < kMaxN; ++j) {
        visited_[i][j] = 0;
      }
    }
  }

  void reset_walk() {
    clear_visited();
    stepCount_ = 0;
    lastDir_ = 0;
    clickPending_ = false;
    const int N = gridSize;
    if (startMode == 0) {
      x_ = N / 2;
      y_ = N / 2;
    } else if (startMode == 1) {
      x_ = rng_.int_range(0, N - 1);
      y_ = rng_.int_range(0, N - 1);
    } else {
      x_ = 0;
      y_ = 0;
    }
    x_ = std::clamp(x_, 0, N - 1);
    y_ = std::clamp(y_, 0, N - 1);
    visited_[x_][y_] = 1;
  }

  // On trap: clear grid and pick a random seed cell (startMode ignored here
  // so repeated traps explore different regions rather than always restarting
  // at the same configured start).
  void reset_walk_pick_random() {
    clear_visited();
    stepCount_ = 0;
    lastDir_ = 0;
    const int N = gridSize;
    x_ = rng_.int_range(0, N - 1);
    y_ = rng_.int_range(0, N - 1);
    x_ = std::clamp(x_, 0, N - 1);
    y_ = std::clamp(y_, 0, N - 1);
    visited_[x_][y_] = 1;
  }

  int sampleRate_;
  Randomizer rng_;

  std::shared_ptr<ValueSource> amplitude_, rate_;
  std::shared_ptr<ValueSource> directionBias_, clickAmount_, ditherAmount_;
  std::shared_ptr<ValueSource> smoothness_;

  uint8_t visited_[kMaxN][kMaxN]{};
  int x_{0};
  int y_{0};
  int stepCount_{0};
  int lastDir_{0};
  int resetCounter_{0};
  bool clickPending_{false};

  float opCredits_{0.0f};
  float smoothedPrev_{0.0f};
  float cur_{0.0f};
};

} // namespace mforce
