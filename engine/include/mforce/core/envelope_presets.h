#pragma once
#include "mforce/core/envelope.h"

namespace mforce {

// ---------------------------------------------------------------------------
// Convenience envelope classes — thin wrappers around Envelope with
// setting_descriptors() for the relevant params. Each rebuilds its stages
// when a config value changes.
//
// Stage min/max timing constraints use sensible defaults internally;
// they're not exposed to the UI (too confusing for interactive use).
// The full table-based Envelope editor will expose per-stage timing.
//
// Per-stage Curve/Power (Matt 2026-08-29): each shaped stage exposes its
// RampType and power as settings (xxxCurve dropdown + xxxPower), applied in
// rebuild() after the stage layout is constructed. Defaults match the old
// hardcoded ramps, so patches that don't set them render identically.
// ---------------------------------------------------------------------------

// Ramp-type dropdown labels. Ordinals match RampType (Linear=0, Expo,
// InverseExpo, Sine) and the generic Envelope stage table's combo.
inline constexpr const char* kRampCurveLabels[] = {
  "Linear", "Expo", "InverseExpo", "Sine", nullptr
};

// Attack → Release (expand). 0→1→0 shape.
// Use for: amplitude envelopes, simple modulation.
struct AREnvelope final : Envelope {
  AREnvelope(int sampleRate) : Envelope(sampleRate), sr_(sampleRate) { rebuild(); }

  const char* type_name() const override { return "AREnvelope"; }

  std::span<const SettingDescriptor> setting_descriptors() const override {
    static constexpr SettingDescriptor descs[] = {
      {"attack",         SettingType::Float, 0.2f, 0.0f, 1.0f},
      {"attackCurve",    SettingType::Int,   0.0f, 0.0f, 3.0f, kRampCurveLabels},
      {"attackPower",    SettingType::Float, 0.0f, 0.0f, 10.0f},
      {"releaseCurve",   SettingType::Int,   3.0f, 0.0f, 3.0f, kRampCurveLabels},
      {"releasePower",   SettingType::Float, 0.0f, 0.0f, 10.0f},
      {"stage_accuracy", SettingType::Float, 1.0f, 0.0f, 1.0f},
      {"ramp_accuracy",  SettingType::Float, 1.0f, 0.0f, 1.0f},
    };
    return descs;
  }

  void set_setting(std::string_view name, float value) override {
    if (name == "attack")       { attack_ = value; rebuild(); return; }
    if (name == "attackCurve")  { attackCurve_ = std::clamp(int(value), 0, 3); rebuild(); return; }
    if (name == "attackPower")  { attackPower_ = value; rebuild(); return; }
    if (name == "releaseCurve") { releaseCurve_ = std::clamp(int(value), 0, 3); rebuild(); return; }
    if (name == "releasePower") { releasePower_ = value; rebuild(); return; }
    Envelope::set_setting(name, value);
  }

  float get_setting(std::string_view name) const override {
    if (name == "attack")       return attack_;
    if (name == "attackCurve")  return float(attackCurve_);
    if (name == "attackPower")  return attackPower_;
    if (name == "releaseCurve") return float(releaseCurve_);
    if (name == "releasePower") return releasePower_;
    return Envelope::get_setting(name);
  }

private:
  void rebuild() {
    Envelope env = Envelope::make_ar(sr_, attack_);
    env.stage(0).ramp.type  = RampType(attackCurve_);
    env.stage(0).ramp.power = attackPower_;
    env.stage(1).ramp.type  = RampType(releaseCurve_);
    env.stage(1).ramp.power = releasePower_;
    replace_stages(std::move(env));
  }
  int sr_;
  float attack_{0.2f};
  int   attackCurve_{0};
  float attackPower_{0.0f};
  int   releaseCurve_{3};
  float releasePower_{0.0f};
};

// Attack → Sustain (expand). 0→level, holds at level.
// Use for: timbral shifts during attack, modulation that ramps then holds.
struct ASEnvelope final : Envelope {
  ASEnvelope(int sampleRate) : Envelope(sampleRate), sr_(sampleRate) { rebuild(); }

  const char* type_name() const override { return "ASEnvelope"; }

  std::span<const SettingDescriptor> setting_descriptors() const override {
    static constexpr SettingDescriptor descs[] = {
      {"attack",         SettingType::Float, 0.2f, 0.0f, 1.0f},
      {"attackCurve",    SettingType::Int,   0.0f, 0.0f, 3.0f, kRampCurveLabels},
      {"attackPower",    SettingType::Float, 0.0f, 0.0f, 10.0f},
      {"sustainLevel",   SettingType::Float, 1.0f, 0.0f, 1.0f},
      {"reverse",        SettingType::Bool,  0.0f, 0.0f, 1.0f},
      {"stage_accuracy", SettingType::Float, 1.0f, 0.0f, 1.0f},
      {"ramp_accuracy",  SettingType::Float, 1.0f, 0.0f, 1.0f},
    };
    return descs;
  }

  void set_setting(std::string_view name, float value) override {
    if (name == "attack")       { attack_ = value; rebuild(); return; }
    if (name == "attackCurve")  { attackCurve_ = std::clamp(int(value), 0, 3); rebuild(); return; }
    if (name == "attackPower")  { attackPower_ = value; rebuild(); return; }
    if (name == "sustainLevel") { sustainLevel_ = value; rebuild(); return; }
    if (name == "reverse")      { reverse_ = (value != 0.0f); rebuild(); return; }
    Envelope::set_setting(name, value);
  }

  float get_setting(std::string_view name) const override {
    if (name == "attack")       return attack_;
    if (name == "attackCurve")  return float(attackCurve_);
    if (name == "attackPower")  return attackPower_;
    if (name == "sustainLevel") return sustainLevel_;
    if (name == "reverse")      return reverse_ ? 1.0f : 0.0f;
    return Envelope::get_setting(name);
  }

private:
  void rebuild() {
    // Normal : 0 → sustainLevel → sustainLevel (rise, then hold)
    // Reverse: sustainLevel → 0 → 0           (fall from peak, then hold at 0)
    // The reverse shape replaces the usual "Range[min=1,max=0] around an
    // ASEnvelope" pattern for "peak at attack, zero at sustain" curves.
    const float startV = reverse_ ? sustainLevel_ : 0.0f;
    const float endV   = reverse_ ? 0.0f          : sustainLevel_;
    Envelope env(sr_);
    env.add_stage({{startV, endV, RampType(attackCurve_), attackPower_}, attack_, 0.05f, 1.0f});
    env.add_stage({{endV, endV, RampType::Linear, 0.0f}, 0.0f, 0.0f, 0.0f});
    replace_stages(std::move(env));
  }
  int sr_;
  float attack_{0.2f}, sustainLevel_{1.0f};
  bool  reverse_{false};
  int   attackCurve_{0};
  float attackPower_{0.0f};
};

// Attack → Sustain (expand) → Release. 0→level→level→0.
// Use for: amplitude with sustain and release.
struct ASREnvelope final : Envelope {
  ASREnvelope(int sampleRate) : Envelope(sampleRate), sr_(sampleRate) { rebuild(); }

  const char* type_name() const override { return "ASREnvelope"; }

  std::span<const SettingDescriptor> setting_descriptors() const override {
    static constexpr SettingDescriptor descs[] = {
      {"attack",         SettingType::Float, 0.2f, 0.0f, 1.0f},
      {"attackCurve",    SettingType::Int,   0.0f, 0.0f, 3.0f, kRampCurveLabels},
      {"attackPower",    SettingType::Float, 0.0f, 0.0f, 10.0f},
      {"sustainLevel",   SettingType::Float, 1.0f, 0.0f, 1.0f},
      {"release",        SettingType::Float, 0.1f, 0.0f, 1.0f},
      {"releaseCurve",   SettingType::Int,   3.0f, 0.0f, 3.0f, kRampCurveLabels},
      {"releasePower",   SettingType::Float, 0.0f, 0.0f, 10.0f},
      {"stage_accuracy", SettingType::Float, 1.0f, 0.0f, 1.0f},
      {"ramp_accuracy",  SettingType::Float, 1.0f, 0.0f, 1.0f},
    };
    return descs;
  }

  void set_setting(std::string_view name, float value) override {
    if (name == "attack")       { attack_ = value; rebuild(); return; }
    if (name == "attackCurve")  { attackCurve_ = std::clamp(int(value), 0, 3); rebuild(); return; }
    if (name == "attackPower")  { attackPower_ = value; rebuild(); return; }
    if (name == "sustainLevel") { sustainLevel_ = value; rebuild(); return; }
    if (name == "release")      { release_ = value; rebuild(); return; }
    if (name == "releaseCurve") { releaseCurve_ = std::clamp(int(value), 0, 3); rebuild(); return; }
    if (name == "releasePower") { releasePower_ = value; rebuild(); return; }
    Envelope::set_setting(name, value);
  }

  float get_setting(std::string_view name) const override {
    if (name == "attack")       return attack_;
    if (name == "attackCurve")  return float(attackCurve_);
    if (name == "attackPower")  return attackPower_;
    if (name == "sustainLevel") return sustainLevel_;
    if (name == "release")      return release_;
    if (name == "releaseCurve") return float(releaseCurve_);
    if (name == "releasePower") return releasePower_;
    return Envelope::get_setting(name);
  }

private:
  void rebuild() {
    Envelope env(sr_);
    env.add_stage({{0.0f, sustainLevel_, RampType(attackCurve_), attackPower_}, attack_, 0.05f, 1.0f});
    env.add_stage({{sustainLevel_, sustainLevel_, RampType::Linear, 0.0f}, 0.0f, 0.0f, 0.0f});
    env.add_stage({{sustainLevel_, 0.0f, RampType(releaseCurve_), releasePower_}, release_, 0.0f, 0.0f});
    replace_stages(std::move(env));
  }
  int sr_;
  float attack_{0.2f}, sustainLevel_{1.0f}, release_{0.1f};
  int   attackCurve_{0};
  float attackPower_{0.0f};
  int   releaseCurve_{3};
  float releasePower_{0.0f};
};

// Attack → Decay → Sustain (expand). 0→1→level, holds.
// Use for: timbral attack transient that decays to a steady value.
struct ADSEnvelope final : Envelope {
  ADSEnvelope(int sampleRate) : Envelope(sampleRate), sr_(sampleRate) { rebuild(); }

  const char* type_name() const override { return "ADSEnvelope"; }

  std::span<const SettingDescriptor> setting_descriptors() const override {
    static constexpr SettingDescriptor descs[] = {
      {"attack",         SettingType::Float, 0.2f, 0.0f, 1.0f},
      {"attackCurve",    SettingType::Int,   0.0f, 0.0f, 3.0f, kRampCurveLabels},
      {"attackPower",    SettingType::Float, 0.0f, 0.0f, 10.0f},
      {"decay",          SettingType::Float, 0.1f, 0.0f, 1.0f},
      {"decayCurve",     SettingType::Int,   0.0f, 0.0f, 3.0f, kRampCurveLabels},
      {"decayPower",     SettingType::Float, 0.0f, 0.0f, 10.0f},
      {"sustainLevel",   SettingType::Float, 0.0f, 0.0f, 1.0f},
      {"stage_accuracy", SettingType::Float, 1.0f, 0.0f, 1.0f},
      {"ramp_accuracy",  SettingType::Float, 1.0f, 0.0f, 1.0f},
    };
    return descs;
  }

  void set_setting(std::string_view name, float value) override {
    if (name == "attack")       { attack_ = value; rebuild(); return; }
    if (name == "attackCurve")  { attackCurve_ = std::clamp(int(value), 0, 3); rebuild(); return; }
    if (name == "attackPower")  { attackPower_ = value; rebuild(); return; }
    if (name == "decay")        { decay_ = value; rebuild(); return; }
    if (name == "decayCurve")   { decayCurve_ = std::clamp(int(value), 0, 3); rebuild(); return; }
    if (name == "decayPower")   { decayPower_ = value; rebuild(); return; }
    if (name == "sustainLevel") { sustainLevel_ = value; rebuild(); return; }
    Envelope::set_setting(name, value);
  }

  float get_setting(std::string_view name) const override {
    if (name == "attack")       return attack_;
    if (name == "attackCurve")  return float(attackCurve_);
    if (name == "attackPower")  return attackPower_;
    if (name == "decay")        return decay_;
    if (name == "decayCurve")   return float(decayCurve_);
    if (name == "decayPower")   return decayPower_;
    if (name == "sustainLevel") return sustainLevel_;
    return Envelope::get_setting(name);
  }

private:
  void rebuild() {
    Envelope env(sr_);
    env.add_stage({{0.0f, 1.0f, RampType(attackCurve_), attackPower_}, attack_, 0.05f, 1.0f});
    env.add_stage({{1.0f, sustainLevel_, RampType(decayCurve_), decayPower_}, decay_, 0.025f, 0.5f});
    env.add_stage({{sustainLevel_, sustainLevel_, RampType::Linear, 0.0f}, 0.0f, 0.0f, 0.0f});
    replace_stages(std::move(env));
  }
  int sr_;
  float attack_{0.2f}, decay_{0.1f}, sustainLevel_{0.0f};
  int   attackCurve_{0};
  float attackPower_{0.0f};
  int   decayCurve_{0};
  float decayPower_{0.0f};
};

// Attack → Decay → Release (expand). 0→1→level→0.
// Use for: percussive sounds with sustain body.
struct ADREnvelope final : Envelope {
  ADREnvelope(int sampleRate) : Envelope(sampleRate), sr_(sampleRate) { rebuild(); }

  const char* type_name() const override { return "ADREnvelope"; }

  std::span<const SettingDescriptor> setting_descriptors() const override {
    static constexpr SettingDescriptor descs[] = {
      {"attack",         SettingType::Float, 0.2f, 0.0f, 1.0f},
      {"attackCurve",    SettingType::Int,   0.0f, 0.0f, 3.0f, kRampCurveLabels},
      {"attackPower",    SettingType::Float, 0.0f, 0.0f, 10.0f},
      {"decay",          SettingType::Float, 0.1f, 0.0f, 1.0f},
      {"decayCurve",     SettingType::Int,   0.0f, 0.0f, 3.0f, kRampCurveLabels},
      {"decayPower",     SettingType::Float, 0.0f, 0.0f, 10.0f},
      {"decayLevel",     SettingType::Float, 0.7f, 0.0f, 1.0f},
      {"releaseCurve",   SettingType::Int,   3.0f, 0.0f, 3.0f, kRampCurveLabels},
      {"releasePower",   SettingType::Float, 0.0f, 0.0f, 10.0f},
      {"stage_accuracy", SettingType::Float, 1.0f, 0.0f, 1.0f},
      {"ramp_accuracy",  SettingType::Float, 1.0f, 0.0f, 1.0f},
    };
    return descs;
  }

  void set_setting(std::string_view name, float value) override {
    if (name == "attack")       { attack_ = value; rebuild(); return; }
    if (name == "attackCurve")  { attackCurve_ = std::clamp(int(value), 0, 3); rebuild(); return; }
    if (name == "attackPower")  { attackPower_ = value; rebuild(); return; }
    if (name == "decay")        { decay_ = value; rebuild(); return; }
    if (name == "decayCurve")   { decayCurve_ = std::clamp(int(value), 0, 3); rebuild(); return; }
    if (name == "decayPower")   { decayPower_ = value; rebuild(); return; }
    if (name == "decayLevel")   { decayLevel_ = value; rebuild(); return; }
    if (name == "releaseCurve") { releaseCurve_ = std::clamp(int(value), 0, 3); rebuild(); return; }
    if (name == "releasePower") { releasePower_ = value; rebuild(); return; }
    Envelope::set_setting(name, value);
  }

  float get_setting(std::string_view name) const override {
    if (name == "attack")       return attack_;
    if (name == "attackCurve")  return float(attackCurve_);
    if (name == "attackPower")  return attackPower_;
    if (name == "decay")        return decay_;
    if (name == "decayCurve")   return float(decayCurve_);
    if (name == "decayPower")   return decayPower_;
    if (name == "decayLevel")   return decayLevel_;
    if (name == "releaseCurve") return float(releaseCurve_);
    if (name == "releasePower") return releasePower_;
    return Envelope::get_setting(name);
  }

private:
  void rebuild() {
    Envelope env(sr_);
    env.add_stage({{0.0f, 1.0f, RampType(attackCurve_), attackPower_}, attack_, 0.05f, 1.0f});
    env.add_stage({{1.0f, decayLevel_, RampType(decayCurve_), decayPower_}, decay_, 0.025f, 0.5f});
    env.add_stage({{decayLevel_, 0.0f, RampType(releaseCurve_), releasePower_}, 0.0f, 0.0f, 0.0f});
    replace_stages(std::move(env));
  }
  int sr_;
  float attack_{0.2f}, decay_{0.1f}, decayLevel_{0.7f};
  int   attackCurve_{0};
  float attackPower_{0.0f};
  int   decayCurve_{0};
  float decayPower_{0.0f};
  int   releaseCurve_{3};
  float releasePower_{0.0f};
};

// Attack → Decay → Sustain (expand) → Release. Full ADSR.
// Use for: standard amplitude envelopes.
struct ADSREnvelope final : Envelope {
  ADSREnvelope(int sampleRate) : Envelope(sampleRate), sr_(sampleRate) { rebuild(); }

  const char* type_name() const override { return "ADSREnvelope"; }

  std::span<const SettingDescriptor> setting_descriptors() const override {
    static constexpr SettingDescriptor descs[] = {
      {"attack",         SettingType::Float, 0.2f, 0.0f, 1.0f},
      {"attackCurve",    SettingType::Int,   0.0f, 0.0f, 3.0f, kRampCurveLabels},
      {"attackPower",    SettingType::Float, 0.0f, 0.0f, 10.0f},
      {"decay",          SettingType::Float, 0.1f, 0.0f, 1.0f},
      {"decayCurve",     SettingType::Int,   0.0f, 0.0f, 3.0f, kRampCurveLabels},
      {"decayPower",     SettingType::Float, 0.0f, 0.0f, 10.0f},
      {"sustainLevel",   SettingType::Float, 0.7f, 0.0f, 1.0f},
      // Default 0.1 (was 0 — Matt 2026-08-29): a fresh ADSR should release,
      // not hard-cut. All corpus patches set release explicitly (verified
      // 2026-08-29), so the default change moves no existing render.
      {"release",        SettingType::Float, 0.1f, 0.0f, 1.0f},
      {"releaseCurve",   SettingType::Int,   3.0f, 0.0f, 3.0f, kRampCurveLabels},
      {"releasePower",   SettingType::Float, 0.0f, 0.0f, 10.0f},
      {"stage_accuracy", SettingType::Float, 1.0f, 0.0f, 1.0f},
      {"ramp_accuracy",  SettingType::Float, 1.0f, 0.0f, 1.0f},
    };
    return descs;
  }

  void set_setting(std::string_view name, float value) override {
    if (name == "attack")       { attack_ = value; rebuild(); return; }
    if (name == "attackCurve")  { attackCurve_ = std::clamp(int(value), 0, 3); rebuild(); return; }
    if (name == "attackPower")  { attackPower_ = value; rebuild(); return; }
    if (name == "decay")        { decay_ = value; rebuild(); return; }
    if (name == "decayCurve")   { decayCurve_ = std::clamp(int(value), 0, 3); rebuild(); return; }
    if (name == "decayPower")   { decayPower_ = value; rebuild(); return; }
    if (name == "sustainLevel") { sustainLevel_ = value; rebuild(); return; }
    if (name == "release")      { release_ = value; rebuild(); return; }
    if (name == "releaseCurve") { releaseCurve_ = std::clamp(int(value), 0, 3); rebuild(); return; }
    if (name == "releasePower") { releasePower_ = value; rebuild(); return; }
    Envelope::set_setting(name, value);
  }

  float get_setting(std::string_view name) const override {
    if (name == "attack")       return attack_;
    if (name == "attackCurve")  return float(attackCurve_);
    if (name == "attackPower")  return attackPower_;
    if (name == "decay")        return decay_;
    if (name == "decayCurve")   return float(decayCurve_);
    if (name == "decayPower")   return decayPower_;
    if (name == "sustainLevel") return sustainLevel_;
    if (name == "release")      return release_;
    if (name == "releaseCurve") return float(releaseCurve_);
    if (name == "releasePower") return releasePower_;
    return Envelope::get_setting(name);
  }

private:
  void rebuild() {
    Envelope env = Envelope::make_adsr(sr_,
        attack_, decay_, sustainLevel_, release_);
    env.stage(0).ramp.type  = RampType(attackCurve_);
    env.stage(0).ramp.power = attackPower_;
    env.stage(1).ramp.type  = RampType(decayCurve_);
    env.stage(1).ramp.power = decayPower_;
    env.stage(3).ramp.type  = RampType(releaseCurve_);
    env.stage(3).ramp.power = releasePower_;
    replace_stages(std::move(env));
  }
  int sr_;
  float attack_{0.2f}, decay_{0.1f}, sustainLevel_{0.7f}, release_{0.1f};
  int   attackCurve_{0};
  float attackPower_{0.0f};
  int   decayCurve_{0};
  float decayPower_{0.0f};
  int   releaseCurve_{3};
  float releasePower_{0.0f};
};

} // namespace mforce
