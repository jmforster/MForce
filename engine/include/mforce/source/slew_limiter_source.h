#pragma once
#include "mforce/core/dsp_value_source.h"
#include <memory>

namespace mforce {

// Rate limiter / lag for control signals. New in the C++ port (no C# original).
//
// Motivation: velvet impulses driving FMSource's `phase` flip the carrier for
// exactly one sample, which is a step discontinuity — maximally bright, with
// no brightness dial that doesn't also change the pitch content. Rate-limiting
// the step turns it into a short ramp; a phase ramp is a frequency offset, so
// the click becomes a chirp whose length (and therefore brightness) is one
// number. Useful anywhere a control signal is too steppy.
//
// mode Slew: |delta| clamped to rate/sample, i.e. a straight ramp. A step of
//   size S takes S/rate seconds and ARRIVES exactly.
// mode Lag:  one-pole, held += (in - held) * clamp(rate*dt, 0, 1) —
//   exponential approach, never exactly arriving, no corner at the top.
// mode Peak: MAGNITUDE-keyed envelope follower. When |in| > |held| the output
//   attacks toward `in` at `rate`; otherwise it decays toward ZERO at
//   `fallRate`. Slew and Lag are sign-keyed and therefore do nothing useful to
//   a one-sample IMPULSE — they merely attenuate it to height rate*dt and it
//   stays one sample wide. Peak turns each impulse into a jump plus a glide
//   back to rest, symmetric for both polarities, which is what makes the
//   velvet-into-FM-phase click a tunable chirp rather than a quieter click.
// `rate` is units/s in Slew and Peak, and 1/s (reciprocal time constant) in
// Lag. The units differ; the direction does not — bigger is faster in all
// three.
//
// fallRate == 0 means "mirror rate". A literally-zero fall rate (freeze on the
// way down) is degenerate; express it as a small positive number.
enum class SlewMode { Slew, Lag, Peak };

struct SlewLimiterSource final : ValueSource {
  SlewMode mode{SlewMode::Slew};

  void set_source(std::shared_ptr<ValueSource> s)   { source_ = std::move(s); }
  void set_rate(std::shared_ptr<ValueSource> s)     { rate_ = std::move(s); }
  void set_fallRate(std::shared_ptr<ValueSource> s) { fallRate_ = std::move(s); }

  std::shared_ptr<ValueSource> get_source() const   { return source_; }
  std::shared_ptr<ValueSource> get_rate() const     { return rate_; }
  std::shared_ptr<ValueSource> get_fallRate() const { return fallRate_; }

  const char* type_name() const override { return "SlewLimiterSource"; }
  SourceCategory category() const override { return SourceCategory::Filter; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"rate",     1000.0f, 0.0f, 1000000.0f, "u/s|1/s"},
      {"fallRate",    0.0f, 0.0f, 1000000.0f, "0=mirror"},
    };
    return descs;
  }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"source"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source")   { source_ = std::move(src); return; }
    if (name == "rate")     { rate_ = std::move(src); return; }
    if (name == "fallRate") { fallRate_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "source")   return source_;
    if (name == "rate")     return rate_;
    if (name == "fallRate") return fallRate_;
    return nullptr;
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr const char* kModeLabels[] = { "Slew", "Lag", "Peak", nullptr };
    static constexpr ConfigDescriptor descs[] = {
      {"mode", ConfigType::Int, 0.0f, 0.0f, 2.0f, kModeLabels},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "mode") {
      const int m = int(value);
      mode = (m == 1) ? SlewMode::Lag : (m == 2) ? SlewMode::Peak : SlewMode::Slew;
      return;
    }
  }

  float get_config(std::string_view name) const override {
    if (name == "mode") return float(static_cast<int>(mode));
    return 0.0f;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    dt_ = (ctx.sampleRate > 0) ? 1.0f / float(ctx.sampleRate) : 0.0f;
    primed_ = false;
    cur_ = 0.0f;
    if (source_)   source_->prepare(ctx, frames);
    if (rate_)     rate_->prepare(ctx, frames);
    if (fallRate_) fallRate_->prepare(ctx, frames);
  }

  float next() override {
    if (source_)   source_->next();
    if (rate_)     rate_->next();
    if (fallRate_) fallRate_->next();

    const float in = source_ ? source_->current() : 0.0f;

    // First sample after prepare adopts the input verbatim — otherwise every
    // note would open with a ramp up from zero, which is an artifact.
    if (!primed_) {
      primed_ = true;
      cur_ = in;
      return cur_;
    }

    const float d = in - cur_;
    const float r = rate_ ? rate_->current() : 1000.0f;
    const float rf = fallRate_ ? fallRate_->current() : 0.0f;

    if (mode == SlewMode::Peak) {
      // Magnitude-keyed: attack toward the input while it is the larger
      // excursion, otherwise decay toward rest. `d` is not consulted for the
      // direction test — that is the whole point of this mode.
      const float ain = (in < 0.0f) ? -in : in;
      const float acur = (cur_ < 0.0f) ? -cur_ : cur_;
      if (ain > acur) {
        float step = r * dt_;
        if (step < 0.0f) step = 0.0f;
        cur_ += (d > step) ? step : ((d < -step) ? -step : d);
      } else {
        float step = ((rf > 0.0f) ? rf : r) * dt_;
        if (step < 0.0f) step = 0.0f;
        if (cur_ > 0.0f)      cur_ = (cur_ > step) ? cur_ - step : 0.0f;
        else if (cur_ < 0.0f) cur_ = (cur_ < -step) ? cur_ + step : 0.0f;
      }
      return cur_;
    }

    // fallRate 0 mirrors rate; anything positive overrides it on the way down.
    float step = ((d < 0.0f) && (rf > 0.0f) ? rf : r) * dt_;
    if (step < 0.0f) step = 0.0f;

    if (mode == SlewMode::Slew) {
      cur_ += (d > step) ? step : ((d < -step) ? -step : d);
    } else {
      cur_ += d * ((step > 1.0f) ? 1.0f : step);
    }
    return cur_;
  }

  float current() const override { return cur_; }

private:
  std::shared_ptr<ValueSource> source_;
  std::shared_ptr<ValueSource> rate_;
  std::shared_ptr<ValueSource> fallRate_;
  float dt_{1.0f / 48000.0f};
  float cur_{0.0f};
  bool primed_{false};
};

} // namespace mforce
