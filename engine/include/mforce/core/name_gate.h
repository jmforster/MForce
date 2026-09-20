#pragma once
#include "mforce/core/dsp_value_source.h"
#include <cmath>
#include <memory>
#include <string>

namespace mforce {

// ---------------------------------------------------------------------------
// NameGate (spec 2026-09-19-note-transitions §5): the visible home of a
// transition-name match. Input = the Note node's `transition` pin (the
// note's interned id, a held level); setting-of-sorts = one name string,
// resolved to `targetId` against the instrument's transitions[] vocabulary
// at load (strings are loader territory — SettingType has no string).
// Output = 1.0 while the current note's transition matches, else 0.0.
//
// Stateless and live: current() computes from in_->current() (the
// PerformOut idiom — Setup-time reads and RefSource copies must see the
// live value). Typical consumer is an Envelope's `trigger` input, which is
// read at note Setup and never pulled per sample; NameGate also behaves
// when wired into an audible path (next() pulls its input once).
// ---------------------------------------------------------------------------
struct NameGate final : ValueSource {
  explicit NameGate(int) {}

  std::shared_ptr<ValueSource> in_;
  float targetId{-1.0f};   // resolved at load; -1 (unresolved) never matches
  std::string name;        // the authored string, kept for save/lint

  const char* type_name() const override { return "NameGate"; }
  SourceCategory category() const override { return SourceCategory::Modulator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"in", 0.0f, 0.0f, 0.0f},
    };
    return descs;
  }
  void set_param(std::string_view n, std::shared_ptr<ValueSource> src) override {
    if (n == "in") in_ = std::move(src);
  }
  std::shared_ptr<ValueSource> get_param(std::string_view n) const override {
    return n == "in" ? in_ : nullptr;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    if (in_) in_->prepare(ctx, frames);
  }
  float next() override {
    if (in_) in_->next();
    return match_();
  }
  float current() const override { return match_(); }

private:
  float match_() const {
    if (!in_ || targetId < 0.0f) return 0.0f;
    return std::lround(in_->current()) == std::lround(targetId) ? 1.0f : 0.0f;
  }
};

} // namespace mforce
