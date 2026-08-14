#pragma once
#include "mforce/core/envelope.h"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace mforce {

// Build an Envelope from preset-form params JSON ({"preset": "adsr", ...}).
// The ONE preset-name dispatch, shared by the engine patch loader and the
// UI graph loader so the two cannot drift (2026-08-13: the UI dispatch was
// a hand-copied subset and silently kept a default ADSR for any preset it
// didn't know — the damper-fires-on-attack audition bug). Throws on
// unknown presets/timeModes and on key combinations that would otherwise
// be silently dead knobs. Stage-list form ({"stages": [...]}) is handled
// by the callers, not here.
inline Envelope envelope_from_preset_json(const nlohmann::json& p,
                                          int sampleRate) {
  const std::string preset = p.value("preset", std::string("ar"));
  if (preset == "ar") {
    return Envelope::make_ar(sampleRate,
        p.value("attack", 0.2f),
        p.value("attackMin", 0.0f), p.value("attackMax", 1.0f));
  }
  if (preset == "adsr") {
    // Two timing semantics (2026-08-06):
    //   default "fraction": attack/decay/release are fractions of note
    //     duration, clamped per stage — unchanged for every existing patch.
    //   "timeMode":"seconds": literal-seconds stages, no clamps
    //     (make_adsr_abs) — the *Min/*Max keys are meaningless there, so
    //     their presence is an error rather than a silently dead knob.
    const std::string timeMode = p.value("timeMode", std::string("fraction"));
    if (timeMode == "seconds") {
      for (const char* k : {"attackMin", "attackMax", "decayMin",
                            "decayMax", "releaseMin", "releaseMax"})
        if (p.contains(k))
          throw std::runtime_error(
              std::string("adsr timeMode=seconds does not take ") + k);
      return Envelope::make_adsr_abs(sampleRate,
          p.value("attack", 0.2f), p.value("decay", 0.1f),
          p.value("sustainLevel", 0.7f), p.value("release", 0.0f));
    }
    if (timeMode != "fraction")
      throw std::runtime_error("Unknown adsr timeMode: " + timeMode);
    // make_adsr takes six randomization-range args that the loader used to
    // drop on the floor, so an adsr preset could not express stage jitter
    // at all (found 2026-08-04 by tools/lint_patches.py). Defaults are
    // make_adsr's own, so patches that omit the keys are unchanged.
    return Envelope::make_adsr(sampleRate,
        p.value("attack", 0.2f), p.value("decay", 0.1f),
        p.value("sustainLevel", 0.7f), p.value("release", 0.0f),
        p.value("attackMin",  0.05f),  p.value("attackMax",  1.0f),
        p.value("decayMin",   0.025f), p.value("decayMax",   0.5f),
        p.value("releaseMin", 0.0f),   p.value("releaseMax", 0.0f));
  }
  throw std::runtime_error("Unknown envelope preset: " + preset);
}

} // namespace mforce
