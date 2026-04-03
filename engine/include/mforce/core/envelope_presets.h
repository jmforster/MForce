#pragma once
#include "mforce/core/envelope.h"

namespace mforce {

// ---------------------------------------------------------------------------
// Convenience envelope classes — thin wrappers around Envelope with
// config_descriptors() for the relevant params. Each rebuilds its stages
// when a config value changes.
//
// Stage min/max timing constraints use sensible defaults internally;
// they're not exposed to the UI (too confusing for interactive use).
// The full table-based Envelope editor will expose per-stage timing.
// ---------------------------------------------------------------------------

// Attack → Release (expand). 0→1→0 shape.
// Use for: amplitude envelopes, simple modulation.
struct AREnvelope final : Envelope {
  AREnvelope(int sampleRate) : Envelope(sampleRate), sr_(sampleRate) { rebuild(); }

  const char* type_name() const override { return "AREnvelope"; }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"attack", ConfigType::Float, 0.2f, 0.0f, 1.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "attack") { attack_ = value; rebuild(); return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "attack") return attack_;
    return 0.0f;
  }

private:
  void rebuild() {
    *static_cast<Envelope*>(this) = Envelope::make_ar(sr_, attack_);
  }
  int sr_;
  float attack_{0.2f};
};

// Attack → Sustain (expand). 0→level, holds at level.
// Use for: timbral shifts during attack, modulation that ramps then holds.
struct ASEnvelope final : Envelope {
  ASEnvelope(int sampleRate) : Envelope(sampleRate), sr_(sampleRate) { rebuild(); }

  const char* type_name() const override { return "ASEnvelope"; }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"attack",       ConfigType::Float, 0.2f, 0.0f, 1.0f},
      {"sustainLevel", ConfigType::Float, 1.0f, 0.0f, 1.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "attack")       { attack_ = value; rebuild(); return; }
    if (name == "sustainLevel") { sustainLevel_ = value; rebuild(); return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "attack")       return attack_;
    if (name == "sustainLevel") return sustainLevel_;
    return 0.0f;
  }

private:
  void rebuild() {
    Envelope env(sr_);
    env.add_stage({{0.0f, sustainLevel_, RampType::Linear, 0.0f}, attack_, 0.05f, 1.0f});
    env.add_stage({{sustainLevel_, sustainLevel_, RampType::Linear, 0.0f}, 0.0f, 0.0f, 0.0f});
    *static_cast<Envelope*>(this) = std::move(env);
  }
  int sr_;
  float attack_{0.2f}, sustainLevel_{1.0f};
};

// Attack → Sustain (expand) → Release. 0→level→level→0.
// Use for: amplitude with sustain and release.
struct ASREnvelope final : Envelope {
  ASREnvelope(int sampleRate) : Envelope(sampleRate), sr_(sampleRate) { rebuild(); }

  const char* type_name() const override { return "ASREnvelope"; }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"attack",       ConfigType::Float, 0.2f, 0.0f, 1.0f},
      {"sustainLevel", ConfigType::Float, 1.0f, 0.0f, 1.0f},
      {"release",      ConfigType::Float, 0.1f, 0.0f, 1.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "attack")       { attack_ = value; rebuild(); return; }
    if (name == "sustainLevel") { sustainLevel_ = value; rebuild(); return; }
    if (name == "release")      { release_ = value; rebuild(); return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "attack")       return attack_;
    if (name == "sustainLevel") return sustainLevel_;
    if (name == "release")      return release_;
    return 0.0f;
  }

private:
  void rebuild() {
    Envelope env(sr_);
    env.add_stage({{0.0f, sustainLevel_, RampType::Linear, 0.0f}, attack_, 0.05f, 1.0f});
    env.add_stage({{sustainLevel_, sustainLevel_, RampType::Linear, 0.0f}, 0.0f, 0.0f, 0.0f});
    env.add_stage({{sustainLevel_, 0.0f, RampType::Sine, 0.0f}, release_, 0.0f, 0.0f});
    *static_cast<Envelope*>(this) = std::move(env);
  }
  int sr_;
  float attack_{0.2f}, sustainLevel_{1.0f}, release_{0.1f};
};

// Attack → Decay → Sustain (expand). 0→1→level, holds.
// Use for: timbral attack transient that decays to a steady value.
struct ADSEnvelope final : Envelope {
  ADSEnvelope(int sampleRate) : Envelope(sampleRate), sr_(sampleRate) { rebuild(); }

  const char* type_name() const override { return "ADSEnvelope"; }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"attack",       ConfigType::Float, 0.2f, 0.0f, 1.0f},
      {"decay",        ConfigType::Float, 0.1f, 0.0f, 1.0f},
      {"sustainLevel", ConfigType::Float, 0.0f, 0.0f, 1.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "attack")       { attack_ = value; rebuild(); return; }
    if (name == "decay")        { decay_ = value; rebuild(); return; }
    if (name == "sustainLevel") { sustainLevel_ = value; rebuild(); return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "attack")       return attack_;
    if (name == "decay")        return decay_;
    if (name == "sustainLevel") return sustainLevel_;
    return 0.0f;
  }

private:
  void rebuild() {
    Envelope env(sr_);
    env.add_stage({{0.0f, 1.0f, RampType::Linear, 0.0f}, attack_, 0.05f, 1.0f});
    env.add_stage({{1.0f, sustainLevel_, RampType::Linear, 0.0f}, decay_, 0.025f, 0.5f});
    env.add_stage({{sustainLevel_, sustainLevel_, RampType::Linear, 0.0f}, 0.0f, 0.0f, 0.0f});
    *static_cast<Envelope*>(this) = std::move(env);
  }
  int sr_;
  float attack_{0.2f}, decay_{0.1f}, sustainLevel_{0.0f};
};

// Attack → Decay → Release (expand). 0→1→level→0.
// Use for: percussive sounds with sustain body.
struct ADREnvelope final : Envelope {
  ADREnvelope(int sampleRate) : Envelope(sampleRate), sr_(sampleRate) { rebuild(); }

  const char* type_name() const override { return "ADREnvelope"; }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"attack",     ConfigType::Float, 0.2f, 0.0f, 1.0f},
      {"decay",      ConfigType::Float, 0.1f, 0.0f, 1.0f},
      {"decayLevel", ConfigType::Float, 0.7f, 0.0f, 1.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "attack")     { attack_ = value; rebuild(); return; }
    if (name == "decay")      { decay_ = value; rebuild(); return; }
    if (name == "decayLevel") { decayLevel_ = value; rebuild(); return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "attack")     return attack_;
    if (name == "decay")      return decay_;
    if (name == "decayLevel") return decayLevel_;
    return 0.0f;
  }

private:
  void rebuild() {
    Envelope env(sr_);
    env.add_stage({{0.0f, 1.0f, RampType::Linear, 0.0f}, attack_, 0.05f, 1.0f});
    env.add_stage({{1.0f, decayLevel_, RampType::Linear, 0.0f}, decay_, 0.025f, 0.5f});
    env.add_stage({{decayLevel_, 0.0f, RampType::Sine, 0.0f}, 0.0f, 0.0f, 0.0f});
    *static_cast<Envelope*>(this) = std::move(env);
  }
  int sr_;
  float attack_{0.2f}, decay_{0.1f}, decayLevel_{0.7f};
};

// Attack → Decay → Sustain (expand) → Release. Full ADSR.
// Use for: standard amplitude envelopes.
struct ADSREnvelope final : Envelope {
  ADSREnvelope(int sampleRate) : Envelope(sampleRate), sr_(sampleRate) { rebuild(); }

  const char* type_name() const override { return "ADSREnvelope"; }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"attack",       ConfigType::Float, 0.2f, 0.0f, 1.0f},
      {"decay",        ConfigType::Float, 0.1f, 0.0f, 1.0f},
      {"sustainLevel", ConfigType::Float, 0.7f, 0.0f, 1.0f},
      {"release",      ConfigType::Float, 0.0f, 0.0f, 1.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "attack")       { attack_ = value; rebuild(); return; }
    if (name == "decay")        { decay_ = value; rebuild(); return; }
    if (name == "sustainLevel") { sustainLevel_ = value; rebuild(); return; }
    if (name == "release")      { release_ = value; rebuild(); return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "attack")       return attack_;
    if (name == "decay")        return decay_;
    if (name == "sustainLevel") return sustainLevel_;
    if (name == "release")      return release_;
    return 0.0f;
  }

private:
  void rebuild() {
    *static_cast<Envelope*>(this) = Envelope::make_adsr(sr_,
        attack_, decay_, sustainLevel_, release_);
  }
  int sr_;
  float attack_{0.2f}, decay_{0.1f}, sustainLevel_{0.7f}, release_{0.0f};
};

} // namespace mforce
