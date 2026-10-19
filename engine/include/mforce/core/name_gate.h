#pragma once
#include "mforce/core/dsp_value_source.h"
#include <cmath>
#include <memory>
#include <string>

namespace mforce {

// ---------------------------------------------------------------------------
// NameGate (spec 2026-09-20-note-onsets-v2 §6): the visible home of an
// onset-name match. Input = the Note node's `onset` pin (the
// note's interned id, a held level); setting-of-sorts = one name string,
// resolved to `targetId` against the instrument's onsets[] vocabulary
// at load (strings are loader territory — SettingType has no string).
// Output = 1.0 while the current note's onset matches, else 0.0.
//
// Stateless and live: current() computes from `in` (the PerformOut
// idiom — a Setup-time read happens between ticks and must see the live
// value, not the previous tick's memo).
// Typical consumer is an Envelope's `trigger` input, which is read at note
// Setup and never pulled per sample; NameGate also behaves when wired into
// an audible path (the base pulls its input once per sample).
// ---------------------------------------------------------------------------
struct NameGate final : Generator {
  Input in{this, "in", Input::Optional};

  float targetId{-1.0f};   // resolved at load; -1 (unresolved) never matches
  std::string name;        // the authored string, kept for save/lint

  Range range() const override { return Range::Unipolar; }
  const char* type_name() const override { return "NameGate"; }
  SourceCategory category() const override { return SourceCategory::Modulator; }

  float compute() override { return match_(); }
  float current() const override { return match_(); }

private:
  float match_() const {
    if (!in.source || targetId < 0.0f) return 0.0f;
    return std::lround(in.current()) == std::lround(targetId) ? 1.0f : 0.0f;
  }
};

} // namespace mforce
