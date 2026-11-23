#pragma once
#include "mforce/contract/pitch_curve.h"
#include "mforce/source/envelope/envelope.h"
#include <memory>

namespace mforce {

// Compile a PitchCurve (Score/Performer semantic) into an Envelope (DSP).
// Linear ramps only; nodes define hold regions, trans straddles boundaries.
//
// Pattern: for N semi[] nodes, the Envelope has up to 2*N-1 stages:
//   hold_0, ramp_{0→1}, hold_1, ramp_{1→2}, ..., hold_{N-1}
// Each hold takes (dur[i] - trans/2 for interior, - trans on one side for
// endpoints). Each ramp takes trans of total duration. Last hold may be
// the "fill" stage (percent=0, expand to remainder).
inline std::shared_ptr<Envelope> compile_pitch_curve(const PitchCurve& curve) {
  curve.validate();
  const size_t n = curve.semi.size();
  auto env = std::make_shared<Envelope>();

  // Fill-last detection: zero dur at last index means expand.
  bool fillLast = (curve.dur.back() == 0.0f);

  for (size_t i = 0; i < n; ++i) {
    const float semi = curve.semi[i];

    // Hold stage duration: dur[i] minus adjacent ramp-halves.
    float holdPct = curve.dur[i];
    if (i > 0)     holdPct -= curve.trans[i - 1] * 0.5f;
    if (i + 1 < n) holdPct -= curve.trans[i] * 0.5f;
    if (holdPct < 0.0f) holdPct = 0.0f;

    bool isFill = (fillLast && i == n - 1);

    Envelope::Stage hold;
    hold.ramp = Ramp{semi, semi, RampType::Linear, 0.0f};
    hold.percent = isFill ? 0.0f : holdPct;  // 0 = expand-to-fill
    env->add_stage(hold);

    if (i + 1 < n) {
      Envelope::Stage ramp;
      ramp.ramp = Ramp{semi, curve.semi[i + 1], RampType::Linear, 0.0f};
      ramp.percent = curve.trans[i];
      env->add_stage(ramp);
    }
  }

  return env;
}

} // namespace mforce
