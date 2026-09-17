#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pierce_allpass.h"
#include <memory>

namespace mforce {

// ---------------------------------------------------------------------------
// PierceFilter — the Pierce/Van Duyne passive nonlinear allpass as a graph
// node, so the same filter that terminates the 2D mesh boundary (Mesh2D
// edgeMode 2) can terminate a 1D delay loop.  All the theory, provenance and
// the passivity numbers live in core/pierce_allpass.h and
// docs/research/stk_port/PIERCE_PASSIVE_NOTES.md.
//
// Why a node of its own rather than a mode on Shaper or pins on Biquad:
// Shaper is a memoryless drawn-curve transfer (plus one bit of stick/slip
// hysteresis) and has no filter state to switch; Biquad is a fixed
// coefficient-computing second-order section whose coefficients are designed
// from fc/Q, not switched per sample off an internal state variable.  This
// filter is a first-order allpass carrying its own state u, and its defining
// behaviour IS that the coefficient is chosen by sign(u) with the state
// carried across the change at constant energy.  Neither existing node has a
// place to put that without changing what it is.  The node is tiny — two pins
// and one POD state — and registry-only.
//
// Pins are the paper's two stiffnesses: `coefNeg` (a_1, in force while the
// internal state is negative) and `coefPos` (a_2, while it is >= 0).  Equal
// values = an exactly linear first-order allpass; the distance between them
// drives the nonlinearity.  Both are read per sample, so they modulate.
//
// Passive for every coefficient pair, so it can run at full drive in a
// feedback loop without a level clamp.
// ---------------------------------------------------------------------------
struct PierceFilterSource final : ValueSource {
  PierceFilterSource()
    : source_(std::make_shared<ConstantSource>(0.0f)),
      coefNeg_(std::make_shared<ConstantSource>(0.5f)),
      coefPos_(std::make_shared<ConstantSource>(-0.5f)) {}

  const char* type_name() const override { return "PierceFilter"; }
  SourceCategory category() const override { return SourceCategory::Filter; }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"source"},
    };
    return descs;
  }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"coefNeg",  0.5f, -PierceAllpass::kCoefMax, PierceAllpass::kCoefMax,
       "stiffness"},
      {"coefPos", -0.5f, -PierceAllpass::kCoefMax, PierceAllpass::kCoefMax,
       "stiffness"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source")  { source_  = std::move(src); return; }
    if (name == "coefNeg") { coefNeg_ = std::move(src); return; }
    if (name == "coefPos") { coefPos_ = std::move(src); return; }
  }
  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "source")  return source_;
    if (name == "coefNeg") return coefNeg_;
    if (name == "coefPos") return coefPos_;
    return nullptr;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    if (source_) source_->prepare(ctx, frames);
    coefNeg_->prepare(ctx, frames);
    coefPos_->prepare(ctx, frames);
    ap_.reset(coefPos_->current());
    cur_ = 0.0f;
  }

  float next() override {
    const float x = source_ ? source_->next() : 0.0f;
    coefNeg_->next();
    coefPos_->next();
    cur_ = ap_.tick(x, coefNeg_->current(), coefPos_->current());
    return cur_;
  }
  float current() const override { return cur_; }

private:
  std::shared_ptr<ValueSource> source_, coefNeg_, coefPos_;
  PierceAllpass ap_;
  float cur_{0.0f};
};

} // namespace mforce
