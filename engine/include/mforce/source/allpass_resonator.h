#pragma once
#include "mforce/core/dsp_value_source.h"
#include <memory>
#include <vector>
#include <cmath>
#include <algorithm>

namespace mforce {

// ---------------------------------------------------------------------------
// AllpassResonator — the four-knob shape Alpha Forever later packaged their
// KS piano variant into (per Sound On Sound's coverage of the 22/01 update,
// see docs/research/alpha_forever.md): a Karplus-Strong loop where
//
//   * the DELAY LINE is replaced by an allpass DIFFUSER — a Schroeder
//     allpass whose internal delay is set by the note pitch and whose
//     feedback coefficient is `tension`. On the resonance grid the diffuser
//     is phase-exact (tuning holds); between grid points its phase is
//     warped, which smears/spreads the loop's upper resonances — the
//     inharmonic clustering that Gyutai originally built from nested
//     allpasses ("Nested allpass filters gave me similar results with a
//     fraction of the work").
//   * a second allpass sits in the feedback loop — `stiffness`
//     (double-real-pole biquad; frequency-dependent delay = dispersion,
//     stretched partials).
//   * a one-pole lowpass models string frequency loss — `damping`.
//   * `feedback` sets the overall decay.
//
// Excitation (`source`) is the hammer chain output, per the AF piano: a
// decaying envelope, not a noise burst. A DC blocker guards the input. A
// first-order Thiran-style tuning allpass absorbs the fractional part of
// the loop delay.
//
// Per-note behavior: frequency captured at first next() after prepare()
// (lazy init, WavetableSource convention); paramMap can retune `frequency`
// and drive any config per note via frequency->curve entries.
// Real-time safe: buffer allocated at construction (sized for ~12 Hz at
// 48k); prepare() zero-fills only; next() allocates nothing.
// ---------------------------------------------------------------------------
struct AllpassResonator final : ValueSource {

  explicit AllpassResonator(int sampleRate) : sampleRate_(sampleRate) {
    frequency_ = std::make_shared<ConstantSource>(220.0f);
    amplitude_ = std::make_shared<ConstantSource>(1.0f);
    buf_.assign(kBufLen, 0.0f);
  }

  const char* type_name() const override { return "AllpassResonator"; }
  SourceCategory category() const override { return SourceCategory::Oscillator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"frequency", 220.0f, 12.0f, 8000.0f, "hz"},
      {"amplitude", 1.0f,   0.0f,  10.0f,   "0-1"},
    };
    return descs;
  }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"source"},
    };
    return descs;
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"feedback",   ConfigType::Float, 0.995f, 0.0f, 0.9999f},
      {"damping",    ConfigType::Float, 0.6f,   0.05f, 1.0f},   // LP coeff
      {"stiffness",  ConfigType::Float, 0.15f,  0.0f, 0.95f},   // AP pole
      {"tension",    ConfigType::Float, 0.35f,  0.0f, 0.95f},   // diffuser fb
      {"exciteGain", ConfigType::Float, 1.0f,   0.0f, 8.0f},
      {"direct",     ConfigType::Float, 0.2f,   0.0f, 1.0f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source")    { source_    = std::move(src); return; }
    if (name == "frequency") { frequency_ = std::move(src); return; }
    if (name == "amplitude") { amplitude_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "source")    return source_;
    if (name == "frequency") return frequency_;
    if (name == "amplitude") return amplitude_;
    return nullptr;
  }

  void set_config(std::string_view name, float v) override {
    if (name == "feedback")   { feedback_   = std::clamp(v, 0.0f, 0.9999f); return; }
    if (name == "damping")    { damping_    = std::clamp(v, 0.05f, 1.0f); return; }
    if (name == "stiffness")  { stiffness_  = std::clamp(v, 0.0f, 0.95f); return; }
    if (name == "tension")    { tension_    = std::clamp(v, 0.0f, 0.95f); return; }
    if (name == "exciteGain") { exciteGain_ = v; return; }
    if (name == "direct")     { direct_     = v; return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "feedback")   return feedback_;
    if (name == "damping")    return damping_;
    if (name == "stiffness")  return stiffness_;
    if (name == "tension")    return tension_;
    if (name == "exciteGain") return exciteGain_;
    if (name == "direct")     return direct_;
    return 0.0f;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    if (source_)    source_->prepare(ctx, frames);
    if (frequency_) frequency_->prepare(ctx, frames);
    if (amplitude_) amplitude_->prepare(ctx, frames);
    std::fill(buf_.begin(), buf_.end(), 0.0f);
    wpos_ = 0;
    bq_ = BiquadState{};
    lp_ = 0.0f;
    tuneX_ = tuneY_ = 0.0f;
    dcX_ = dcY_ = 0.0f;
    fbSample_ = 0.0f;
    initialized_ = false;
    cur_ = 0.0f;
  }

  float next() override {
    if (frequency_) frequency_->next();
    if (amplitude_) amplitude_->next();
    float exc = 0.0f;
    if (source_) { source_->next(); exc = source_->current(); }

    if (!initialized_) init_note();

    // DC blocker on excitation
    float x = dcR_ * dcY_ + exc - dcX_;
    dcX_ = exc; dcY_ = x;
    x *= exciteGain_;

    // Diffuser: Schroeder allpass of length loopLen_, feedback = tension.
    float in = x + fbSample_;
    int rd = wpos_ - loopLen_;
    if (rd < 0) rd += kBufLen;
    float dOut = buf_[rd];
    float v = in + tension_ * dOut;
    float y = -tension_ * v + dOut;
    buf_[wpos_] = v;
    wpos_ = (wpos_ + 1) % kBufLen;

    // Fractional tuning allpass (Thiran 1st order)
    float yt = tuneA_ * y + tuneX_ - tuneA_ * tuneY_;
    tuneX_ = y; tuneY_ = yt;

    // Stiffness allpass (double-real-pole biquad)
    float ys = yt;
    if (stiffness_ > 0.001f) {
      const float a = stiffness_;
      ys = a * a * yt - 2.0f * a * bq_.x1 + bq_.x2
         + 2.0f * a * bq_.y1 - a * a * bq_.y2;
      bq_.x2 = bq_.x1; bq_.x1 = yt;
      bq_.y2 = bq_.y1; bq_.y1 = ys;
    }

    // Damping lowpass + decay feedback (applied next sample)
    lp_ += damping_ * (ys - lp_);
    fbSample_ = feedback_ * lp_;

    float ampl = amplitude_ ? amplitude_->current() : 1.0f;
    cur_ = (yt + direct_ * x) * ampl;
    return cur_;
  }

  float current() const override { return cur_; }

private:
  static constexpr int kBufLen = 4096;

  struct BiquadState { float x1{0}, x2{0}, y1{0}, y2{0}; };

  static float ap1_phase_delay(float a, float w) {
    float cw = std::cos(w), sw = std::sin(w);
    float ph = std::atan2(-sw, cw - a) - std::atan2(a * sw, 1.0f - a * cw);
    return (w > 1e-6f) ? -ph / w : (1.0f + a) / (1.0f - a);
  }

  void init_note() {
    float f0 = frequency_ ? frequency_->current() : 220.0f;
    f0 = std::clamp(f0, 12.0f, float(sampleRate_) * 0.4f);
    const float sr = float(sampleRate_);
    const float period = sr / f0;
    const float w0 = 2.0f * 3.14159265f * f0 / sr;

    // Loop budget: total phase delay at f0 must equal one period:
    // tau_diffuser(M) + tau_stiff + tau_lp + tau_tune + 1 (fb latency) = P.
    // The diffuser's phase delay is NOT simply M when tension > 0 — off the
    // resonance grid the tension allpass warps phase:
    //   tau_diff(M) = M + (2/w0) * atan(g sin(M w0) / (1 - g cos(M w0)))
    // so M is found by fixed-point iteration (converges in a few steps for
    // the tensions in range).
    // One biquad = 2 first-order allpass sections at pole `stiffness`.
    float apDelay = stiffness_ > 0.001f
        ? 2.0f * ap1_phase_delay(stiffness_, w0) : 0.0f;
    float lpDelay;
    {
      float b = 1.0f - damping_;
      float cw = std::cos(w0), sw = std::sin(w0);
      lpDelay = std::atan2(b * sw, 1.0f - b * cw) / w0;
    }
    float target = period - apDelay - lpDelay - 1.0f;
    // Damped fixed point: the warp derivative can exceed 1 near the grid
    // (slope up to 2g/(1-g)), so undamped iteration diverges there. Damping
    // lambda = (1-g)/(1+g) makes the worst-case slope ~0.
    const float g = tension_;
    const float lam = (1.0f - g) / (1.0f + g);
    float M = target;
    for (int it = 0; it < 80; ++it) {
      float mw = M * w0;
      float warp = 2.0f * std::atan2(g * std::sin(mw), 1.0f - g * std::cos(mw)) / w0;
      M += lam * ((target - warp) - M);
    }
    M = std::clamp(M, 2.0f, float(kBufLen - 4));
    loopLen_ = int(M);
    float frac = M - float(loopLen_);
    tuneA_ = (1.0f - frac) / (1.0f + frac);

    dcR_ = 1.0f - 2.0f * 3.14159265f * 20.0f / sr;
    initialized_ = true;
  }

  std::shared_ptr<ValueSource> source_;
  std::shared_ptr<ValueSource> frequency_;
  std::shared_ptr<ValueSource> amplitude_;
  int sampleRate_;

  // Config (the four AF knobs + I/O trim)
  float feedback_{0.995f};
  float damping_{0.6f};
  float stiffness_{0.15f};
  float tension_{0.35f};
  float exciteGain_{1.0f};
  float direct_{0.2f};

  // Per-note state
  bool  initialized_{false};
  std::vector<float> buf_;
  int   wpos_{0};
  int   loopLen_{100};
  float tuneA_{0.0f};
  float tuneX_{0.0f}, tuneY_{0.0f};
  BiquadState bq_;
  float lp_{0.0f};
  float fbSample_{0.0f};
  float dcR_{0.997f};
  float dcX_{0.0f}, dcY_{0.0f};
  float cur_{0.0f};
};

} // namespace mforce
