#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <cmath>
#include <memory>

namespace mforce {

// ---------------------------------------------------------------------------
// HomotopySource — homotopy continuation tracker for a 2D nonlinear system.
//
//   F0(x,y) = { x^2 + y^2 - 1, x - y }            (simple, solved at (√½, √½))
//   F1(x,y) = { x^2 + y^2 - r, x*y - s }          (target with parameters)
//   H(x,y,t) = (1-t) F0 + t F1
//
// The source tracks the root (x,y) of H(·,·,t)=0 as t sweeps 0→1 with a
// predictor-corrector scheme: tangent step then Newton correction. At t=0
// or t=1 the sweep reverses, producing a continuous drone that slowly
// explores parameter space. Bifurcations (near-singular Jacobian) manifest
// as residual spikes on output mode 0.
// ---------------------------------------------------------------------------
struct HomotopySource final : ValueSource {
  int outputMode{1}; // 0 = residual norm, 1 = x, 2 = y, 3 = t*2-1

  HomotopySource(int sampleRate, uint32_t seed = 0x40A0'0001u)
  : sampleRate_(sampleRate), rng_(seed),
    amplitude_(std::make_shared<ConstantSource>(0.5f)),
    rate_(std::make_shared<ConstantSource>(48000.0f)),
    stepSize_(std::make_shared<ConstantSource>(0.001f)),
    rTarget_(std::make_shared<ConstantSource>(2.0f)),
    sTarget_(std::make_shared<ConstantSource>(0.5f)),
    newtonTol_(std::make_shared<ConstantSource>(1e-5f)),
    smoothness_(std::make_shared<ConstantSource>(0.0f))
  {
    reset_state();
  }

  const char* type_name() const override { return "HomotopySource"; }
  SourceCategory category() const override { return SourceCategory::Oscillator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"amplitude",  0.5f,     0.0f,   10.0f},
      {"rate",       48000.0f, 0.0f,   2000000.0f},
      {"stepSize",   0.001f,   1e-6f,  0.1f},
      {"rTarget",    2.0f,     0.0f,   10.0f},
      {"sTarget",    0.5f,     -5.0f,  5.0f},
      {"newtonTol",  1e-5f,    1e-9f,  1e-2f},
      {"smoothness", 0.0f,     0.0f,   0.999f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "amplitude")  { amplitude_  = std::move(src); return; }
    if (name == "rate")       { rate_       = std::move(src); return; }
    if (name == "stepSize")   { stepSize_   = std::move(src); return; }
    if (name == "rTarget")    { rTarget_    = std::move(src); return; }
    if (name == "sTarget")    { sTarget_    = std::move(src); return; }
    if (name == "newtonTol")  { newtonTol_  = std::move(src); return; }
    if (name == "smoothness") { smoothness_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "amplitude")  return amplitude_;
    if (name == "rate")       return rate_;
    if (name == "stepSize")   return stepSize_;
    if (name == "rTarget")    return rTarget_;
    if (name == "sTarget")    return sTarget_;
    if (name == "newtonTol")  return newtonTol_;
    if (name == "smoothness") return smoothness_;
    return nullptr;
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"outputMode", ConfigType::Int, 1.0f, 0.0f, 3.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "outputMode") { outputMode = std::clamp(int(value), 0, 3); return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "outputMode") return float(outputMode);
    return 0.0f;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    if (amplitude_)  amplitude_->prepare(ctx, frames);
    if (rate_)       rate_->prepare(ctx, frames);
    if (stepSize_)   stepSize_->prepare(ctx, frames);
    if (rTarget_)    rTarget_->prepare(ctx, frames);
    if (sTarget_)    sTarget_->prepare(ctx, frames);
    if (newtonTol_)  newtonTol_->prepare(ctx, frames);
    if (smoothness_) smoothness_->prepare(ctx, frames);
  }

  float next() override {
    amplitude_->next(); rate_->next();
    stepSize_->next(); rTarget_->next(); sTarget_->next();
    newtonTol_->next(); smoothness_->next();

    float rateHz = std::max(0.0f, rate_->current());
    opCredits_ += rateHz / float(sampleRate_);

    while (opCredits_ >= 1.0f) {
      continuation_step();
      opCredits_ -= 1.0f;
    }

    float resNorm = residual_norm(x_, y_, float(t_));

    float raw = 0.0f;
    switch (outputMode) {
      case 0: raw = resNorm; break;
      case 1: raw = x_; break;
      case 2: raw = y_; break;
      case 3: raw = float(t_) * 2.0f - 1.0f; break;
    }

    raw = std::clamp(raw, -5.0f, 5.0f);

    float s = std::clamp(smoothness_->current(), 0.0f, 0.999f);
    float smoothed = (1.0f - s) * raw + s * smoothedPrev_;
    smoothedPrev_ = smoothed;

    cur_ = smoothed * amplitude_->current();
    return cur_;
  }

  float current() const override { return cur_; }

private:
  void reset_state() {
    const float inv_sqrt2 = 0.70710678118654752440f;
    x_ = inv_sqrt2;
    y_ = inv_sqrt2;
    t_ = 0.0;
    sweepDir_ = +1;
    stepsInNewton_ = 0;
    lastSingular_ = false;
  }

  // Evaluate H(x,y,t) = (1-t)*F0 + t*F1.
  //   F0 = (x^2 + y^2 - 1,  x - y)
  //   F1 = (x^2 + y^2 - r,  x*y - s)
  // Into (h1, h2).
  void eval_H(float x, float y, float t, float r, float s,
              float& h1, float& h2) const {
    float f01 = x*x + y*y - 1.0f;
    float f02 = x - y;
    float f11 = x*x + y*y - r;
    float f12 = x*y - s;
    h1 = (1.0f - t) * f01 + t * f11;
    h2 = (1.0f - t) * f02 + t * f12;
  }

  // Partials wrt (x,y): returns 2x2 Jacobian [[a,b],[c,d]].
  //   dF0/d(x,y) = [[2x, 2y], [1, -1]]
  //   dF1/d(x,y) = [[2x, 2y], [y,  x]]
  void eval_Jxy(float x, float y, float t,
                float& a, float& b, float& c, float& d) const {
    float j0a = 2.0f * x, j0b = 2.0f * y;
    float j0c = 1.0f,     j0d = -1.0f;
    float j1a = 2.0f * x, j1b = 2.0f * y;
    float j1c = y,        j1d = x;
    a = (1.0f - t) * j0a + t * j1a;
    b = (1.0f - t) * j0b + t * j1b;
    c = (1.0f - t) * j0c + t * j1c;
    d = (1.0f - t) * j0d + t * j1d;
  }

  // Partial wrt t: returns dH/dt = F1 - F0.
  void eval_Ht(float x, float y, float r, float s,
               float& e, float& f) const {
    float f01 = x*x + y*y - 1.0f;
    float f02 = x - y;
    float f11 = x*x + y*y - r;
    float f12 = x*y - s;
    e = f11 - f01;
    f = f12 - f02;
  }

  float residual_norm(float x, float y, float t) const {
    float r = std::max(0.0f, rTarget_->current());
    float s = sTarget_->current();
    float h1, h2;
    eval_H(x, y, t, r, s, h1, h2);
    return std::sqrt(h1*h1 + h2*h2);
  }

  // Solve [a b; c d] [dx; dy] = [p; q] via Cramer's rule.
  //   dx = ( p*d - b*q) / det
  //   dy = ( a*q - c*p) / det
  // Returns false if singular.
  static bool solve2x2(float a, float b, float c, float d,
                       float p, float q,
                       float& dx, float& dy) {
    float det = a*d - b*c;
    if (std::fabs(det) < 1e-10f) return false;
    dx = (p*d - b*q) / det;
    dy = (a*q - c*p) / det;
    return true;
  }

  void continuation_step() {
    float step = std::max(1e-6f, stepSize_->current());
    float rTgt = std::max(0.0f, rTarget_->current());
    float sTgt = sTarget_->current();
    float tol  = std::max(1e-9f, newtonTol_->current());

    // --- Predictor (tangent step) ---
    //   J_xy * [dx; dy] = -dH/dt
    float a, b, c, d;
    eval_Jxy(x_, y_, float(t_), a, b, c, d);
    float ht_x, ht_y;
    eval_Ht(x_, y_, rTgt, sTgt, ht_x, ht_y);
    float dx, dy;
    bool ok = solve2x2(a, b, c, d, -ht_x, -ht_y, dx, dy);

    if (ok) {
      x_ += float(sweepDir_) * step * dx;
      y_ += float(sweepDir_) * step * dy;
      lastSingular_ = false;
    } else {
      // Near bifurcation: skip predictor update for (x,y) this step.
      lastSingular_ = true;
    }
    t_ += double(sweepDir_) * double(step);

    // --- Reverse at endpoints ---
    if (t_ >= 1.0) {
      t_ = 1.0;
      sweepDir_ = -1;
    } else if (t_ <= 0.0) {
      t_ = 0.0;
      sweepDir_ = +1;
    }

    // --- Corrector (Newton, up to 3 iterations) ---
    stepsInNewton_ = 0;
    for (int it = 0; it < 3; ++it) {
      float h1, h2;
      eval_H(x_, y_, float(t_), rTgt, sTgt, h1, h2);
      float resN = std::sqrt(h1*h1 + h2*h2);
      if (resN < tol) break;

      float ja, jb, jc, jd;
      eval_Jxy(x_, y_, float(t_), ja, jb, jc, jd);
      float cx, cy;
      if (!solve2x2(ja, jb, jc, jd, -h1, -h2, cx, cy)) {
        lastSingular_ = true;
        break;
      }
      x_ += cx;
      y_ += cy;
      ++stepsInNewton_;
    }

    // --- Safety clamps / NaN guard ---
    if (!std::isfinite(x_) || !std::isfinite(y_)) {
      reset_state();
      return;
    }
    x_ = std::clamp(x_, -10.0f, 10.0f);
    y_ = std::clamp(y_, -10.0f, 10.0f);
    if (t_ < 0.0) t_ = 0.0;
    if (t_ > 1.0) t_ = 1.0;
  }

  int sampleRate_;
  Randomizer rng_;

  std::shared_ptr<ValueSource> amplitude_, rate_;
  std::shared_ptr<ValueSource> stepSize_, rTarget_, sTarget_, newtonTol_;
  std::shared_ptr<ValueSource> smoothness_;

  // Tracked root + continuation parameter.
  float x_{0.70710678118654752440f};
  float y_{0.70710678118654752440f};
  double t_{0.0};
  int sweepDir_{+1};
  int stepsInNewton_{0};
  bool lastSingular_{false};

  float opCredits_{0.0f};
  float smoothedPrev_{0.0f};
  float cur_{0.0f};
};

} // namespace mforce
