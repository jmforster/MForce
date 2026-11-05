#pragma once
#include "mforce/source/envelope/envelope.h"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace mforce {

// The preset form of an Envelope node: {"preset": "adsr", ...knob keys...}.
// One table of six layouts over Envelope's factories, shared by the engine
// patch loader and the UI graph loader so the two cannot drift. A builder
// reads its layout's keys with the layout's defaults, lays the stages out
// through the factory, then shapes each curved stage from its "<stage>Curve"
// (0 Linear, 1 Expo, 2 InverseExpo, 3 Sine) and "<stage>Power" keys. Throws
// on an unknown preset, a curve outside 0..3, and on key combinations that
// would otherwise be silently dead knobs. The stage-list form
// ({"stages": [...]}) is the callers' to handle.

namespace envelope_form {

using nlohmann::json;

inline RampType curve(const json& p, const std::string& key, int fallback) {
  const int v = p.value(key, fallback);
  if (v < 0 || v > 3)
    throw std::runtime_error("envelope preset: " + key + " is " + std::to_string(v)
                             + "; a curve is 0 (Linear), 1 (Expo), 2 (InverseExpo) or 3 (Sine)");
  return RampType(v);
}

// Stage `i` takes "<name>Curve" (default `fallback`) and "<name>Power" (default 0).
inline void shape(Envelope& env, int i, const json& p, const std::string& name, int fallback) {
  env.stage(i).ramp.type  = curve(p, name + "Curve", fallback);
  env.stage(i).ramp.power = p.value(name + "Power", 0.0f);
}

inline std::shared_ptr<Envelope> ar(const json& p) {
  auto env = Envelope::make_ar(p.value("attack", 0.2f),
                               p.value("attackMin", 0.0f), p.value("attackMax", 1.0f));
  shape(*env, 0, p, "attack", 0);
  shape(*env, 1, p, "release", 3);
  return env;
}

inline std::shared_ptr<Envelope> as(const json& p) {
  auto env = Envelope::make_as(p.value("attack", 0.2f), p.value("sustainLevel", 1.0f),
                               p.value("reverse", false));
  shape(*env, 0, p, "attack", 0);
  return env;
}

inline std::shared_ptr<Envelope> asr(const json& p) {
  auto env = Envelope::make_asr(p.value("attack", 0.2f), p.value("sustainLevel", 1.0f),
                                p.value("release", 0.1f));
  shape(*env, 0, p, "attack", 0);
  shape(*env, 2, p, "release", 3);
  return env;
}

inline std::shared_ptr<Envelope> ads(const json& p) {
  auto env = Envelope::make_ads(p.value("attack", 0.2f), p.value("decay", 0.1f),
                                p.value("sustainLevel", 0.0f));
  shape(*env, 0, p, "attack", 0);
  shape(*env, 1, p, "decay", 0);
  return env;
}

inline std::shared_ptr<Envelope> adr(const json& p) {
  auto env = Envelope::make_adr(p.value("attack", 0.2f), p.value("decay", 0.1f),
                                p.value("decayLevel", 0.7f));
  shape(*env, 0, p, "attack", 0);
  shape(*env, 1, p, "decay", 0);
  shape(*env, 2, p, "release", 3);
  return env;
}

// Two timing semantics. Default "fraction": attack/decay/release are
// fractions of note duration, clamped per stage. "timeMode": "seconds":
// literal-seconds stages with no clamps (make_adsr_abs), so the *Min/*Max
// keys are meaningless there and their presence is an error rather than a
// silently dead knob. The six clamp keys default to make_adsr's own.
inline std::shared_ptr<Envelope> adsr(const json& p) {
  const std::string timeMode = p.value("timeMode", std::string("fraction"));
  std::shared_ptr<Envelope> env;
  if (timeMode == "seconds") {
    for (const char* k : {"attackMin", "attackMax", "decayMin",
                          "decayMax", "releaseMin", "releaseMax"})
      if (p.contains(k))
        throw std::runtime_error(std::string("adsr timeMode=seconds does not take ") + k);
    env = Envelope::make_adsr_abs(
        p.value("attack", 0.2f), p.value("decay", 0.1f),
        p.value("sustainLevel", 0.7f), p.value("release", 0.0f));
  } else if (timeMode == "fraction") {
    env = Envelope::make_adsr(
        p.value("attack", 0.2f), p.value("decay", 0.1f),
        p.value("sustainLevel", 0.7f), p.value("release", 0.0f),
        p.value("attackMin",  0.0f),   p.value("attackMax",  1.0f),
        p.value("decayMin",   0.0f),   p.value("decayMax",   0.5f),
        p.value("releaseMin", 0.0f),   p.value("releaseMax", 0.0f));
  } else {
    throw std::runtime_error("Unknown adsr timeMode: " + timeMode);
  }
  shape(*env, 0, p, "attack", 0);
  shape(*env, 1, p, "decay", 0);
  shape(*env, 3, p, "release", 3);
  return env;
}

} // namespace envelope_form

struct EnvelopePreset {
  const char* name;
  std::shared_ptr<Envelope> (*build)(const nlohmann::json&);
};

inline constexpr EnvelopePreset kEnvelopePresets[] = {
  {"ar",   &envelope_form::ar},
  {"as",   &envelope_form::as},
  {"asr",  &envelope_form::asr},
  {"ads",  &envelope_form::ads},
  {"adr",  &envelope_form::adr},
  {"adsr", &envelope_form::adsr},
};

inline std::shared_ptr<Envelope> envelope_from_preset_json(const nlohmann::json& p) {
  const std::string preset = p.value("preset", std::string("ar"));
  for (const auto& e : kEnvelopePresets)
    if (preset == e.name) return e.build(p);
  throw std::runtime_error("Unknown envelope preset: " + preset);
}

// Every key the preset form reads, for the loaders' key checks.
inline const std::vector<std::string>& envelope_preset_keys() {
  static const std::vector<std::string> keys = [] {
    std::vector<std::string> v;
    for (const char* k : {"preset", "attack", "attackCurve", "attackPower", "attackMin", "attackMax",
                          "decay", "decayCurve", "decayPower", "decayMin", "decayMax", "decayLevel",
                          "release", "releaseCurve", "releasePower", "releaseMin", "releaseMax",
                          "reverse", "sustainLevel", "timeMode"})
      v.emplace_back(k);
    return v;
  }();
  return keys;
}

} // namespace mforce
