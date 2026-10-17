#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pierce_allpass.h"

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
struct PierceFilterSource final : Processor {
  Param coefNeg{this, "coefNeg",  0.5f, -PierceAllpass::kCoefMax, PierceAllpass::kCoefMax,
                "stiffness"};
  Param coefPos{this, "coefPos", -0.5f, -PierceAllpass::kCoefMax, PierceAllpass::kCoefMax,
                "stiffness"};

  const char* type_name() const override { return "PierceFilter"; }

  void on_prepare(const RenderContext&, int) override {
    ap_.reset(coefPos.current());
    cur_ = 0.0f;
  }

  float compute() override {
    return ap_.tick(input.current(), coefNeg.current(), coefPos.current());
  }

private:
  PierceAllpass ap_;
};

} // namespace mforce
