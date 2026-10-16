#pragma once
#include "mforce/core/envelope.h"

namespace mforce {

// ---------------------------------------------------------------------------
// Convenience envelope classes — thin wrappers around Envelope that declare
// the settings of one stage layout. Each rebuilds its stages when one of
// its own settings changes; the knobs every Envelope owns (stage_accuracy,
// ramp_accuracy, timeScale) are the base's, and so is sustainLevel, which a
// preset with a sustain stage stores and rebuilds from.
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
  Setting<float> attack      {this, "attack",       0.2f, 0.0f, 1.0f};
  Setting<int>   attackCurve {this, "attackCurve",  0,    0,    3, kRampCurveLabels};
  Setting<float> attackPower {this, "attackPower",  0.0f, 0.0f, 10.0f};
  Setting<int>   releaseCurve{this, "releaseCurve", 3,    0,    3, kRampCurveLabels};
  Setting<float> releasePower{this, "releasePower", 0.0f, 0.0f, 10.0f};

  AREnvelope(int sampleRate) : Envelope(sampleRate), sr_(sampleRate) { rebuild(); }

  const char* type_name() const override { return "AREnvelope"; }

  void on_setting_changed(std::string_view name) override {
    attackCurve.value  = std::clamp(attackCurve.value, 0, 3);
    releaseCurve.value = std::clamp(releaseCurve.value, 0, 3);
    if (is_common_knob_(name) || name == "sustainLevel") Envelope::on_setting_changed(name);
    else rebuild();
  }

private:
  void rebuild() {
    auto env = Envelope::make_ar(sr_, attack.value);
    env->stage(0).ramp.type  = RampType(attackCurve.value);
    env->stage(0).ramp.power = attackPower.value;
    env->stage(1).ramp.type  = RampType(releaseCurve.value);
    env->stage(1).ramp.power = releasePower.value;
    replace_stages(std::move(*env));
  }
  int sr_;
};

// Attack → Sustain (expand). 0→level, holds at level.
// Use for: timbral shifts during attack, modulation that ramps then holds.
struct ASEnvelope final : Envelope {
  Setting<float> attack      {this, "attack",      0.2f, 0.0f, 1.0f};
  Setting<int>   attackCurve {this, "attackCurve", 0,    0,    3, kRampCurveLabels};
  Setting<float> attackPower {this, "attackPower", 0.0f, 0.0f, 10.0f};
  Setting<bool>  reverse     {this, "reverse",     false, false, true};

  ASEnvelope(int sampleRate) : Envelope(sampleRate, 1.0f), sr_(sampleRate) { rebuild(); }

  const char* type_name() const override { return "ASEnvelope"; }

  void on_setting_changed(std::string_view name) override {
    attackCurve.value = std::clamp(attackCurve.value, 0, 3);
    if (is_common_knob_(name)) Envelope::on_setting_changed(name);
    else rebuild();
  }

protected:
  float sustain_level() const override { return sustainLevel.value; }

private:
  void rebuild() {
    // Normal : 0 → sustainLevel → sustainLevel (rise, then hold)
    // Reverse: sustainLevel → 0 → 0           (fall from peak, then hold at 0)
    // The reverse shape replaces the usual "Range[min=1,max=0] around an
    // ASEnvelope" pattern for "peak at attack, zero at sustain" curves.
    const float startV = reverse.value ? sustainLevel.value : 0.0f;
    const float endV   = reverse.value ? 0.0f               : sustainLevel.value;
    Envelope env(sr_);
    env.add_stage({{startV, endV, RampType(attackCurve.value), attackPower.value}, attack.value, 0.0f, 1.0f});
    env.add_stage({{endV, endV, RampType::Linear, 0.0f}, 0.0f, 0.0f, 0.0f});
    replace_stages(std::move(env));
  }
  int sr_;
};

// Attack → Sustain (expand) → Release. 0→level→level→0.
// Use for: amplitude with sustain and release.
struct ASREnvelope final : Envelope {
  Setting<float> attack      {this, "attack",       0.2f, 0.0f, 1.0f};
  Setting<int>   attackCurve {this, "attackCurve",  0,    0,    3, kRampCurveLabels};
  Setting<float> attackPower {this, "attackPower",  0.0f, 0.0f, 10.0f};
  Setting<float> release     {this, "release",      0.1f, 0.0f, 1.0f};
  Setting<int>   releaseCurve{this, "releaseCurve", 3,    0,    3, kRampCurveLabels};
  Setting<float> releasePower{this, "releasePower", 0.0f, 0.0f, 10.0f};

  ASREnvelope(int sampleRate) : Envelope(sampleRate, 1.0f), sr_(sampleRate) { rebuild(); }

  const char* type_name() const override { return "ASREnvelope"; }

  void on_setting_changed(std::string_view name) override {
    attackCurve.value  = std::clamp(attackCurve.value, 0, 3);
    releaseCurve.value = std::clamp(releaseCurve.value, 0, 3);
    if (is_common_knob_(name)) Envelope::on_setting_changed(name);
    else rebuild();
  }

protected:
  float sustain_level() const override { return sustainLevel.value; }

private:
  void rebuild() {
    const float level = sustainLevel.value;
    Envelope env(sr_);
    env.add_stage({{0.0f, level, RampType(attackCurve.value), attackPower.value}, attack.value, 0.0f, 1.0f});
    env.add_stage({{level, level, RampType::Linear, 0.0f}, 0.0f, 0.0f, 0.0f});
    env.add_stage({{level, 0.0f, RampType(releaseCurve.value), releasePower.value}, release.value, 0.0f, 0.0f});
    replace_stages(std::move(env));
  }
  int sr_;
};

// Attack → Decay → Sustain (expand). 0→1→level, holds.
// Use for: timbral attack transient that decays to a steady value.
struct ADSEnvelope final : Envelope {
  Setting<float> attack     {this, "attack",      0.2f, 0.0f, 1.0f};
  Setting<int>   attackCurve{this, "attackCurve", 0,    0,    3, kRampCurveLabels};
  Setting<float> attackPower{this, "attackPower", 0.0f, 0.0f, 10.0f};
  Setting<float> decay      {this, "decay",       0.1f, 0.0f, 1.0f};
  Setting<int>   decayCurve {this, "decayCurve",  0,    0,    3, kRampCurveLabels};
  Setting<float> decayPower {this, "decayPower",  0.0f, 0.0f, 10.0f};

  ADSEnvelope(int sampleRate) : Envelope(sampleRate, 0.0f), sr_(sampleRate) { rebuild(); }

  const char* type_name() const override { return "ADSEnvelope"; }

  void on_setting_changed(std::string_view name) override {
    attackCurve.value = std::clamp(attackCurve.value, 0, 3);
    decayCurve.value  = std::clamp(decayCurve.value, 0, 3);
    if (is_common_knob_(name)) Envelope::on_setting_changed(name);
    else rebuild();
  }

protected:
  float sustain_level() const override { return sustainLevel.value; }

private:
  void rebuild() {
    const float level = sustainLevel.value;
    Envelope env(sr_);
    env.add_stage({{0.0f, 1.0f, RampType(attackCurve.value), attackPower.value}, attack.value, 0.0f, 1.0f});
    env.add_stage({{1.0f, level, RampType(decayCurve.value), decayPower.value}, decay.value, 0.0f, 0.5f});
    env.add_stage({{level, level, RampType::Linear, 0.0f}, 0.0f, 0.0f, 0.0f});
    replace_stages(std::move(env));
  }
  int sr_;
};

// Attack → Decay → Release (expand). 0→1→level→0.
// Use for: percussive sounds with sustain body.
struct ADREnvelope final : Envelope {
  Setting<float> attack      {this, "attack",       0.2f, 0.0f, 1.0f};
  Setting<int>   attackCurve {this, "attackCurve",  0,    0,    3, kRampCurveLabels};
  Setting<float> attackPower {this, "attackPower",  0.0f, 0.0f, 10.0f};
  Setting<float> decay       {this, "decay",        0.1f, 0.0f, 1.0f};
  Setting<int>   decayCurve  {this, "decayCurve",   0,    0,    3, kRampCurveLabels};
  Setting<float> decayPower  {this, "decayPower",   0.0f, 0.0f, 10.0f};
  Setting<float> decayLevel  {this, "decayLevel",   0.7f, 0.0f, 1.0f};
  Setting<int>   releaseCurve{this, "releaseCurve", 3,    0,    3, kRampCurveLabels};
  Setting<float> releasePower{this, "releasePower", 0.0f, 0.0f, 10.0f};

  ADREnvelope(int sampleRate) : Envelope(sampleRate), sr_(sampleRate) { rebuild(); }

  const char* type_name() const override { return "ADREnvelope"; }

  void on_setting_changed(std::string_view name) override {
    attackCurve.value  = std::clamp(attackCurve.value, 0, 3);
    decayCurve.value   = std::clamp(decayCurve.value, 0, 3);
    releaseCurve.value = std::clamp(releaseCurve.value, 0, 3);
    if (is_common_knob_(name) || name == "sustainLevel") Envelope::on_setting_changed(name);
    else rebuild();
  }

private:
  void rebuild() {
    Envelope env(sr_);
    env.add_stage({{0.0f, 1.0f, RampType(attackCurve.value), attackPower.value}, attack.value, 0.0f, 1.0f});
    env.add_stage({{1.0f, decayLevel.value, RampType(decayCurve.value), decayPower.value}, decay.value, 0.0f, 0.5f});
    env.add_stage({{decayLevel.value, 0.0f, RampType(releaseCurve.value), releasePower.value}, 0.0f, 0.0f, 0.0f});
    replace_stages(std::move(env));
  }
  int sr_;
};

// Attack → Decay → Sustain (expand) → Release. Full ADSR.
// Use for: standard amplitude envelopes.
struct ADSREnvelope final : Envelope {
  Setting<float> attack      {this, "attack",       0.2f, 0.0f, 1.0f};
  Setting<int>   attackCurve {this, "attackCurve",  0,    0,    3, kRampCurveLabels};
  Setting<float> attackPower {this, "attackPower",  0.0f, 0.0f, 10.0f};
  Setting<float> decay       {this, "decay",        0.1f, 0.0f, 1.0f};
  Setting<int>   decayCurve  {this, "decayCurve",   0,    0,    3, kRampCurveLabels};
  Setting<float> decayPower  {this, "decayPower",   0.0f, 0.0f, 10.0f};
  // Default 0.1 (was 0 — Matt 2026-08-29): a fresh ADSR should release,
  // not hard-cut. All corpus patches set release explicitly (verified
  // 2026-08-29), so the default change moves no existing render.
  Setting<float> release     {this, "release",      0.1f, 0.0f, 1.0f};
  Setting<int>   releaseCurve{this, "releaseCurve", 3,    0,    3, kRampCurveLabels};
  Setting<float> releasePower{this, "releasePower", 0.0f, 0.0f, 10.0f};

  ADSREnvelope(int sampleRate) : Envelope(sampleRate, 0.7f), sr_(sampleRate) { rebuild(); }

  const char* type_name() const override { return "ADSREnvelope"; }

  void on_setting_changed(std::string_view name) override {
    attackCurve.value  = std::clamp(attackCurve.value, 0, 3);
    decayCurve.value   = std::clamp(decayCurve.value, 0, 3);
    releaseCurve.value = std::clamp(releaseCurve.value, 0, 3);
    if (is_common_knob_(name)) Envelope::on_setting_changed(name);
    else rebuild();
  }

protected:
  float sustain_level() const override { return sustainLevel.value; }

private:
  void rebuild() {
    auto env = Envelope::make_adsr(sr_,
        attack.value, decay.value, sustainLevel.value, release.value);
    env->stage(0).ramp.type  = RampType(attackCurve.value);
    env->stage(0).ramp.power = attackPower.value;
    env->stage(1).ramp.type  = RampType(decayCurve.value);
    env->stage(1).ramp.power = decayPower.value;
    env->stage(3).ramp.type  = RampType(releaseCurve.value);
    env->stage(3).ramp.power = releasePower.value;
    replace_stages(std::move(*env));
  }
  int sr_;
};

} // namespace mforce
