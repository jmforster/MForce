#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <cmath>
#include <memory>

namespace mforce {

// ---------------------------------------------------------------------------
// MarkovOdeSource — Markov-switched bank of limit-cycle ODEs sharing state.
//
// A single 3D state vector {x, y, z} is integrated by one of four ODEs at
// every integration step. At each step, with probability `switchProb` the
// active ODE is replaced by a uniformly-random pick from the enabled bank.
// Because state is shared across ODEs (no re-init on switch), transitions
// are continuous in state space — only the vector field changes, which
// yields timbral evolution without discontinuous jumps.
//
// Unlike pure-noise chaotic sources, each constituent ODE has a natural
// limit cycle, so output tends to be pitched rather than hissy.
//
// Bank (bit positions in bankMask):
//   0 — Van der Pol  (uses x,y; z decays)
//   1 — Rössler      (uses x,y,z)
//   2 — Duffing      (uses x,y; z decays)
//   3 — Lorenz       (uses x,y,z — scaled down)
//
// Integration is forward Euler with `dt` time per step. `rate` sets how many
// steps per second of audio (opCredits_ pattern, same as FHN).
// ---------------------------------------------------------------------------
struct MarkovOdeSource final : ValueSource {
  int bankMask{15};      // bit 0..3 => VdP, Rössler, Duffing, Lorenz
  int outputMode{0};     // 0=x, 1=y, 2=z, 3=sqrt(x²+y²+z²)/10

  MarkovOdeSource(int sampleRate, uint32_t seed = 0xF417'0004u)
  : sampleRate_(sampleRate), rng_(seed),
    amplitude_(std::make_shared<ConstantSource>(0.3f)),
    rate_(std::make_shared<ConstantSource>(float(sampleRate))),
    dt_(std::make_shared<ConstantSource>(0.01f)),
    switchProb_(std::make_shared<ConstantSource>(0.001f)),
    mu_(std::make_shared<ConstantSource>(2.0f)),
    aR_(std::make_shared<ConstantSource>(0.2f)),
    bR_(std::make_shared<ConstantSource>(0.2f)),
    cR_(std::make_shared<ConstantSource>(5.7f)),
    alpha_(std::make_shared<ConstantSource>(1.0f)),
    beta_(std::make_shared<ConstantSource>(1.0f)),
    delta_(std::make_shared<ConstantSource>(0.1f)),
    sigma_(std::make_shared<ConstantSource>(10.0f)),
    rho_(std::make_shared<ConstantSource>(28.0f)),
    betaL_(std::make_shared<ConstantSource>(2.667f)),
    smoothness_(std::make_shared<ConstantSource>(0.0f))
  {
    reset_state();
  }

  const char* type_name() const override { return "MarkovOdeSource"; }
  SourceCategory category() const override { return SourceCategory::Oscillator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"amplitude",  0.3f,     0.0f,     10.0f},
      {"rate",       48000.0f, 0.0f,     500000.0f},
      {"dt",         0.01f,    0.00001f, 1.0f},
      {"switchProb", 0.001f,   0.0f,     1.0f},
      {"mu",         2.0f,     0.0f,     10.0f},
      {"aR",         0.2f,     -2.0f,    2.0f},
      {"bR",         0.2f,     -2.0f,    2.0f},
      {"cR",         5.7f,     0.0f,     20.0f},
      {"alpha",      1.0f,     -10.0f,   10.0f},
      {"beta",       1.0f,     -10.0f,   10.0f},
      {"delta",      0.1f,     0.0f,     2.0f},
      {"sigma",      10.0f,    0.0f,     50.0f},
      {"rho",        28.0f,    0.0f,     100.0f},
      {"betaL",      2.667f,   0.0f,     20.0f},
      {"smoothness", 0.0f,     0.0f,     0.999f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "amplitude")  { amplitude_  = std::move(src); return; }
    if (name == "rate")       { rate_       = std::move(src); return; }
    if (name == "dt")         { dt_         = std::move(src); return; }
    if (name == "switchProb") { switchProb_ = std::move(src); return; }
    if (name == "mu")         { mu_         = std::move(src); return; }
    if (name == "aR")         { aR_         = std::move(src); return; }
    if (name == "bR")         { bR_         = std::move(src); return; }
    if (name == "cR")         { cR_         = std::move(src); return; }
    if (name == "alpha")      { alpha_      = std::move(src); return; }
    if (name == "beta")       { beta_       = std::move(src); return; }
    if (name == "delta")      { delta_      = std::move(src); return; }
    if (name == "sigma")      { sigma_      = std::move(src); return; }
    if (name == "rho")        { rho_        = std::move(src); return; }
    if (name == "betaL")      { betaL_      = std::move(src); return; }
    if (name == "smoothness") { smoothness_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "amplitude")  return amplitude_;
    if (name == "rate")       return rate_;
    if (name == "dt")         return dt_;
    if (name == "switchProb") return switchProb_;
    if (name == "mu")         return mu_;
    if (name == "aR")         return aR_;
    if (name == "bR")         return bR_;
    if (name == "cR")         return cR_;
    if (name == "alpha")      return alpha_;
    if (name == "beta")       return beta_;
    if (name == "delta")      return delta_;
    if (name == "sigma")      return sigma_;
    if (name == "rho")        return rho_;
    if (name == "betaL")      return betaL_;
    if (name == "smoothness") return smoothness_;
    return nullptr;
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"bankMask",   ConfigType::Int, 15.0f, 0.0f, 15.0f},
      {"outputMode", ConfigType::Int, 0.0f,  0.0f, 3.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "bankMask") {
      bankMask = std::clamp(int(value), 0, 15);
      // If current ODE is no longer enabled, pick a new one (or idle).
      if (bankMask != 0 && !ode_enabled(currentOde_)) {
        currentOde_ = first_enabled_ode();
      }
      return;
    }
    if (name == "outputMode") { outputMode = std::clamp(int(value), 0, 3); return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "bankMask")   return float(bankMask);
    if (name == "outputMode") return float(outputMode);
    return 0.0f;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    if (amplitude_)  amplitude_->prepare(ctx, frames);
    if (rate_)       rate_->prepare(ctx, frames);
    if (dt_)         dt_->prepare(ctx, frames);
    if (switchProb_) switchProb_->prepare(ctx, frames);
    if (mu_)         mu_->prepare(ctx, frames);
    if (aR_)         aR_->prepare(ctx, frames);
    if (bR_)         bR_->prepare(ctx, frames);
    if (cR_)         cR_->prepare(ctx, frames);
    if (alpha_)      alpha_->prepare(ctx, frames);
    if (beta_)       beta_->prepare(ctx, frames);
    if (delta_)      delta_->prepare(ctx, frames);
    if (sigma_)      sigma_->prepare(ctx, frames);
    if (rho_)        rho_->prepare(ctx, frames);
    if (betaL_)      betaL_->prepare(ctx, frames);
    if (smoothness_) smoothness_->prepare(ctx, frames);
  }

  float next() override {
    amplitude_->next(); rate_->next(); dt_->next(); switchProb_->next();
    mu_->next(); aR_->next(); bR_->next(); cR_->next();
    alpha_->next(); beta_->next(); delta_->next();
    sigma_->next(); rho_->next(); betaL_->next();
    smoothness_->next();

    // If nothing is enabled, output silence and don't integrate.
    if (bankMask == 0) {
      cur_ = 0.0f;
      return cur_;
    }

    float rateHz = std::max(0.0f, rate_->current());
    opCredits_ += rateHz / float(sampleRate_);

    while (opCredits_ >= 1.0f) {
      integrate_one_step();
      opCredits_ -= 1.0f;
    }

    float raw = 0.0f;
    switch (outputMode) {
      case 0: raw = x_; break;
      case 1: raw = y_; break;
      case 2: raw = z_; break;
      case 3: raw = std::sqrt(x_*x_ + y_*y_ + z_*z_) * 0.1f; break;
    }

    float s = std::clamp(smoothness_->current(), 0.0f, 0.999f);
    float smoothed = (1.0f - s) * raw + s * smoothedPrev_;
    smoothedPrev_ = smoothed;

    cur_ = smoothed * amplitude_->current();
    return cur_;
  }

  float current() const override { return cur_; }

private:
  // Maybe pick a new ODE according to switchProb. If the current ODE is
  // disabled by bankMask, switch unconditionally.
  void maybe_switch() {
    if (bankMask == 0) return;
    int enabledCount = popcount4(bankMask);
    bool currentDisabled = !ode_enabled(currentOde_);
    float prob = std::clamp(switchProb_->current(), 0.0f, 1.0f);

    if (currentDisabled) {
      currentOde_ = pick_enabled_ode();
      return;
    }
    if (enabledCount <= 1) return;           // nothing to switch to
    if (!rng_.decide(prob)) return;          // no switch this step

    // Pick a random enabled ODE different from the current one.
    int pick = pick_enabled_ode();
    // Best-effort "different than current": try a couple of times.
    for (int tries = 0; tries < 4 && pick == currentOde_; ++tries) {
      pick = pick_enabled_ode();
    }
    currentOde_ = pick;
  }

  void integrate_one_step() {
    maybe_switch();
    if (!ode_enabled(currentOde_)) return;   // belt-and-braces

    float dt = std::max(0.0f, dt_->current());
    float dx = 0.0f, dy = 0.0f, dz = 0.0f;

    switch (currentOde_) {
      case 0: {
        // Van der Pol: dx = y, dy = mu*(1 - x²)*y - x, dz = -0.1*z
        float mu = mu_->current();
        dx = y_;
        dy = mu * (1.0f - x_*x_) * y_ - x_;
        dz = -0.1f * z_;
        break;
      }
      case 1: {
        // Rössler: dx = -y - z, dy = x + a*y, dz = b + z*(x - c)
        float a = aR_->current();
        float b = bR_->current();
        float c = cR_->current();
        dx = -y_ - z_;
        dy = x_ + a * y_;
        dz = b + z_ * (x_ - c);
        break;
      }
      case 2: {
        // Duffing unforced: dx = y, dy = -delta*y + alpha*x - beta*x³,
        //                   dz = -0.1*z
        float alpha = alpha_->current();
        float beta  = beta_->current();
        float delta = delta_->current();
        dx = y_;
        dy = -delta * y_ + alpha * x_ - beta * x_ * x_ * x_;
        dz = -0.1f * z_;
        break;
      }
      case 3: {
        // Lorenz: dx = sigma*(y-x), dy = x*(rho-z) - y, dz = x*y - betaL*z
        // State ranges are large; output scaling by /20 is applied below
        // via scaled Euler step so the shared state stays bounded.
        float sigma = sigma_->current();
        float rho   = rho_->current();
        float bL    = betaL_->current();
        dx = sigma * (y_ - x_);
        dy = x_ * (rho - z_) - y_;
        dz = x_ * y_ - bL * z_;
        // Scale derivatives down so Lorenz plays in roughly the same state
        // regime as the other ODEs (keeps clamping from kicking in every
        // step and prevents sign-flip pops when switching).
        constexpr float kLorenzScale = 1.0f / 20.0f;
        dx *= kLorenzScale;
        dy *= kLorenzScale;
        dz *= kLorenzScale;
        break;
      }
      default: break;
    }

    float nx = x_ + dt * dx;
    float ny = y_ + dt * dy;
    float nz = z_ + dt * dz;

    // NaN/inf guard — reset on blowup.
    if (!std::isfinite(nx) || !std::isfinite(ny) || !std::isfinite(nz)) {
      reset_state();
      cur_ = 0.0f;
      return;
    }

    x_ = std::clamp(nx, -20.0f, 20.0f);
    y_ = std::clamp(ny, -20.0f, 20.0f);
    z_ = std::clamp(nz, -20.0f, 20.0f);
  }

  void reset_state() {
    x_ = 0.1f; y_ = 0.1f; z_ = 0.1f;
    currentOde_ = 0;
    opCredits_ = 0.0f;
    smoothedPrev_ = 0.0f;
  }

  bool ode_enabled(int idx) const {
    if (idx < 0 || idx > 3) return false;
    return (bankMask & (1 << idx)) != 0;
  }

  int first_enabled_ode() const {
    for (int i = 0; i < 4; ++i) if (bankMask & (1 << i)) return i;
    return 0;
  }

  // Uniform pick over enabled ODEs. Caller guarantees bankMask != 0.
  int pick_enabled_ode() {
    int n = popcount4(bankMask);
    if (n <= 0) return 0;
    int k = rng_.int_range(0, n - 1);
    for (int i = 0; i < 4; ++i) {
      if (bankMask & (1 << i)) {
        if (k == 0) return i;
        --k;
      }
    }
    return first_enabled_ode();
  }

  static int popcount4(int m) {
    int c = 0;
    for (int i = 0; i < 4; ++i) if (m & (1 << i)) ++c;
    return c;
  }

  int sampleRate_;
  Randomizer rng_;

  std::shared_ptr<ValueSource> amplitude_, rate_, dt_, switchProb_;
  std::shared_ptr<ValueSource> mu_;
  std::shared_ptr<ValueSource> aR_, bR_, cR_;
  std::shared_ptr<ValueSource> alpha_, beta_, delta_;
  std::shared_ptr<ValueSource> sigma_, rho_, betaL_;
  std::shared_ptr<ValueSource> smoothness_;

  float x_{0.1f}, y_{0.1f}, z_{0.1f};
  int   currentOde_{0};
  float opCredits_{0.0f};
  float smoothedPrev_{0.0f};
  float cur_{0.0f};
};

} // namespace mforce
