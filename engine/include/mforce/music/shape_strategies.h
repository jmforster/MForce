#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/figures.h"
#include "mforce/music/templates.h"
#include "mforce/music/rhythm_util.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <cmath>

namespace mforce {

// ---------------------------------------------------------------------------
// Shape strategies — one FigureStrategy subclass per named figure shape.
//
// Each class's realize_figure emits the MelodicFigure the corresponding
// FigureBuilder method would produce. Bodies mirror the current
// DefaultFigureStrategy::generate_shaped_figure switch cases verbatim,
// with the per-case local variable setup (seed, fb, dir, p1, p2, count)
// pulled inline into each class body.
//
// Used via Composer's registry lookup by name:
//   "shape_scalar_run", "shape_triadic_outline", etc.
// ---------------------------------------------------------------------------

// ====== ShapeScalarRunStrategy ==============================================
class ShapeScalarRunStrategy : public Strategy {
public:
  std::string name() const override { return "shape_scalar_run"; }
  StrategyLevel level() const override { return StrategyLevel::Figure; }
  MelodicFigure realize_figure(const FigureTemplate& ft, StrategyContext& ctx) override;
};

inline MelodicFigure ShapeScalarRunStrategy::realize_figure(
    const FigureTemplate& ft, StrategyContext& ctx) {
  uint32_t seed = ft.seed ? ft.seed : ctx.rng->rng();
  FigureBuilder fb(seed);
  fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  int dir = ft.shapeDirection;
  int count = (ft.maxNotes > ft.minNotes)
      ? Randomizer(seed + 99).int_range(ft.minNotes, ft.maxNotes)
      : (ft.minNotes > 0 ? ft.minNotes : 4);
  return fb.scalar_run(dir, count > 0 ? count : 4, fb.defaultPulse);
}

// ====== ShapeRepeatedNoteStrategy ===========================================
class ShapeRepeatedNoteStrategy : public Strategy {
public:
  std::string name() const override { return "shape_repeated_note"; }
  StrategyLevel level() const override { return StrategyLevel::Figure; }
  MelodicFigure realize_figure(const FigureTemplate& ft, StrategyContext& ctx) override;
};

inline MelodicFigure ShapeRepeatedNoteStrategy::realize_figure(
    const FigureTemplate& ft, StrategyContext& ctx) {
  uint32_t seed = ft.seed ? ft.seed : ctx.rng->rng();
  FigureBuilder fb(seed);
  fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  int count = (ft.maxNotes > ft.minNotes)
      ? Randomizer(seed + 99).int_range(ft.minNotes, ft.maxNotes)
      : (ft.minNotes > 0 ? ft.minNotes : 4);
  return fb.repeated_note(count > 0 ? count : 3, fb.defaultPulse);
}

// ====== ShapeHeldNoteStrategy ===============================================
class ShapeHeldNoteStrategy : public Strategy {
public:
  std::string name() const override { return "shape_held_note"; }
  StrategyLevel level() const override { return StrategyLevel::Figure; }
  MelodicFigure realize_figure(const FigureTemplate& ft, StrategyContext& ctx) override;
};

inline MelodicFigure ShapeHeldNoteStrategy::realize_figure(
    const FigureTemplate& ft, StrategyContext& ctx) {
  uint32_t seed = ft.seed ? ft.seed : ctx.rng->rng();
  FigureBuilder fb(seed);
  fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  return fb.held_note(ft.totalBeats > 0 ? ft.totalBeats : fb.defaultPulse * 2);
}

// ====== ShapeCadentialApproachStrategy ======================================
class ShapeCadentialApproachStrategy : public Strategy {
public:
  std::string name() const override { return "shape_cadential_approach"; }
  StrategyLevel level() const override { return StrategyLevel::Figure; }
  MelodicFigure realize_figure(const FigureTemplate& ft, StrategyContext& ctx) override;
};

inline MelodicFigure ShapeCadentialApproachStrategy::realize_figure(
    const FigureTemplate& ft, StrategyContext& ctx) {
  uint32_t seed = ft.seed ? ft.seed : ctx.rng->rng();
  Randomizer rng(seed);

  float totalBeats = (ft.totalBeats > 0) ? ft.totalBeats : 4.0f;
  float pulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  int approachDir = (ft.shapeDirection < 0) ? -1 : 1;  // -1 = from above, +1 = from below
  // targetSteps: net movement during the approach (negative = descend to target, positive = ascend)
  int targetSteps = (ft.targetNet != 0) ? ft.targetNet :
                    ((ft.shapeParam > 0) ? ft.shapeParam * (-approachDir) : -3 * approachDir);

  // Reserve last portion for the arrival note (longer than approach notes).
  // Clamp to a standard duration that still leaves room for at least one approach note.
  float arrivalDuration = std::min(totalBeats * 0.4f, std::max(pulse * 2.0f, 1.0f));
  static const float STD_DURS[] = {0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f};
  float bestArrival = 1.0f;
  float bestDist = 999.0f;
  for (float s : STD_DURS) {
    if (s <= totalBeats - 0.25f && std::abs(s - arrivalDuration) < bestDist) {
      bestDist = std::abs(s - arrivalDuration);
      bestArrival = s;
    }
  }
  arrivalDuration = bestArrival;

  float approachBeats = totalBeats - arrivalDuration;
  if (approachBeats < 0.25f) {
    // No room for approach notes — emit just the arrival on the current pitch.
    MelodicFigure fig;
    fig.units.push_back({totalBeats, 0});
    return fig;
  }

  // Generate approach rhythm
  auto approachRhythm = generate_musical_rhythm(approachBeats, pulse, rng);
  int approachCount = int(approachRhythm.size());

  // Overshoot-then-recover: 30% of the time when there are enough approach notes
  bool overshoot = rng.decide(0.3f) && approachCount >= 3;

  MelodicFigure fig;
  int stepsRemaining = targetSteps;

  for (int i = 0; i < approachCount; ++i) {
    FigureUnit u;
    u.duration = approachRhythm[i];

    if (i == 0) {
      u.step = 0;  // first note at cursor
    } else if (overshoot && i == 1) {
      // Overshoot: move briefly away from the target direction
      u.step = -approachDir * (rng.decide(0.5f) ? 1 : 2);
      stepsRemaining -= u.step;
    } else {
      // Normal approach: mostly stepwise, occasionally a skip
      int stepSize = rng.decide(0.7f) ? 1 : 2;
      u.step = (stepsRemaining > 0) ? stepSize : (stepsRemaining < 0) ? -stepSize : 0;
      stepsRemaining -= u.step;
    }

    fig.units.push_back(u);
  }

  // Arrival note — lands on whatever remains of targetSteps
  FigureUnit arrival;
  arrival.duration = arrivalDuration;
  arrival.step = stepsRemaining;
  fig.units.push_back(arrival);

  return fig;
}

// ====== ShapeTriadicOutlineStrategy =========================================
class ShapeTriadicOutlineStrategy : public Strategy {
public:
  std::string name() const override { return "shape_triadic_outline"; }
  StrategyLevel level() const override { return StrategyLevel::Figure; }
  MelodicFigure realize_figure(const FigureTemplate& ft, StrategyContext& ctx) override;
};

inline MelodicFigure ShapeTriadicOutlineStrategy::realize_figure(
    const FigureTemplate& ft, StrategyContext& ctx) {
  uint32_t seed = ft.seed ? ft.seed : ctx.rng->rng();
  FigureBuilder fb(seed);
  fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  int dir = ft.shapeDirection;
  int p1 = ft.shapeParam;
  return fb.triadic_outline(dir, p1 > 0, fb.defaultPulse);
}

// ====== ShapeNeighborToneStrategy ===========================================
class ShapeNeighborToneStrategy : public Strategy {
public:
  std::string name() const override { return "shape_neighbor_tone"; }
  StrategyLevel level() const override { return StrategyLevel::Figure; }
  MelodicFigure realize_figure(const FigureTemplate& ft, StrategyContext& ctx) override;
};

inline MelodicFigure ShapeNeighborToneStrategy::realize_figure(
    const FigureTemplate& ft, StrategyContext& ctx) {
  uint32_t seed = ft.seed ? ft.seed : ctx.rng->rng();
  FigureBuilder fb(seed);
  fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  int dir = ft.shapeDirection;
  return fb.neighbor_tone(dir > 0, fb.defaultPulse);
}

// ====== ShapeLeapAndFillStrategy ============================================
class ShapeLeapAndFillStrategy : public Strategy {
public:
  std::string name() const override { return "shape_leap_and_fill"; }
  StrategyLevel level() const override { return StrategyLevel::Figure; }
  MelodicFigure realize_figure(const FigureTemplate& ft, StrategyContext& ctx) override;
};

inline MelodicFigure ShapeLeapAndFillStrategy::realize_figure(
    const FigureTemplate& ft, StrategyContext& ctx) {
  uint32_t seed = ft.seed ? ft.seed : ctx.rng->rng();
  FigureBuilder fb(seed);
  fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  int dir = ft.shapeDirection;
  int p1 = ft.shapeParam;
  int p2 = ft.shapeParam2;
  return fb.leap_and_fill(p1 > 0 ? p1 : 4, dir > 0, p2, fb.defaultPulse);
}

// ====== ShapeScalarReturnStrategy ===========================================
class ShapeScalarReturnStrategy : public Strategy {
public:
  std::string name() const override { return "shape_scalar_return"; }
  StrategyLevel level() const override { return StrategyLevel::Figure; }
  MelodicFigure realize_figure(const FigureTemplate& ft, StrategyContext& ctx) override;
};

inline MelodicFigure ShapeScalarReturnStrategy::realize_figure(
    const FigureTemplate& ft, StrategyContext& ctx) {
  uint32_t seed = ft.seed ? ft.seed : ctx.rng->rng();
  FigureBuilder fb(seed);
  fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  int dir = ft.shapeDirection;
  int p1 = ft.shapeParam;
  int p2 = ft.shapeParam2;
  return fb.scalar_return(dir, p1 > 0 ? p1 : 3, p2, fb.defaultPulse);
}

// ====== ShapeAnacrusisStrategy ==============================================
class ShapeAnacrusisStrategy : public Strategy {
public:
  std::string name() const override { return "shape_anacrusis"; }
  StrategyLevel level() const override { return StrategyLevel::Figure; }
  MelodicFigure realize_figure(const FigureTemplate& ft, StrategyContext& ctx) override;
};

inline MelodicFigure ShapeAnacrusisStrategy::realize_figure(
    const FigureTemplate& ft, StrategyContext& ctx) {
  uint32_t seed = ft.seed ? ft.seed : ctx.rng->rng();
  FigureBuilder fb(seed);
  fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  int dir = ft.shapeDirection;
  int count = (ft.maxNotes > ft.minNotes)
      ? Randomizer(seed + 99).int_range(ft.minNotes, ft.maxNotes)
      : (ft.minNotes > 0 ? ft.minNotes : 4);
  return fb.anacrusis(count > 0 ? count : 2, dir,
                      fb.defaultPulse * 0.5f, fb.defaultPulse);
}

// ====== ShapeZigzagStrategy =================================================
class ShapeZigzagStrategy : public Strategy {
public:
  std::string name() const override { return "shape_zigzag"; }
  StrategyLevel level() const override { return StrategyLevel::Figure; }
  MelodicFigure realize_figure(const FigureTemplate& ft, StrategyContext& ctx) override;
};

inline MelodicFigure ShapeZigzagStrategy::realize_figure(
    const FigureTemplate& ft, StrategyContext& ctx) {
  uint32_t seed = ft.seed ? ft.seed : ctx.rng->rng();
  FigureBuilder fb(seed);
  fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  int dir = ft.shapeDirection;
  int p1 = ft.shapeParam;
  return fb.zigzag(dir, p1 > 0 ? p1 : 3, 2, 1, fb.defaultPulse);
}

// ====== ShapeFanfareStrategy ================================================
class ShapeFanfareStrategy : public Strategy {
public:
  std::string name() const override { return "shape_fanfare"; }
  StrategyLevel level() const override { return StrategyLevel::Figure; }
  MelodicFigure realize_figure(const FigureTemplate& ft, StrategyContext& ctx) override;
};

inline MelodicFigure ShapeFanfareStrategy::realize_figure(
    const FigureTemplate& ft, StrategyContext& ctx) {
  uint32_t seed = ft.seed ? ft.seed : ctx.rng->rng();
  FigureBuilder fb(seed);
  fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  int p1 = ft.shapeParam;
  return fb.fanfare({4, 3}, p1 > 0 ? p1 : 1, fb.defaultPulse);
}

// ====== ShapeSighStrategy ===================================================
class ShapeSighStrategy : public Strategy {
public:
  std::string name() const override { return "shape_sigh"; }
  StrategyLevel level() const override { return StrategyLevel::Figure; }
  MelodicFigure realize_figure(const FigureTemplate& ft, StrategyContext& ctx) override;
};

inline MelodicFigure ShapeSighStrategy::realize_figure(
    const FigureTemplate& ft, StrategyContext& ctx) {
  uint32_t seed = ft.seed ? ft.seed : ctx.rng->rng();
  FigureBuilder fb(seed);
  fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  return fb.sigh(fb.defaultPulse);
}

// ====== ShapeSuspensionStrategy =============================================
class ShapeSuspensionStrategy : public Strategy {
public:
  std::string name() const override { return "shape_suspension"; }
  StrategyLevel level() const override { return StrategyLevel::Figure; }
  MelodicFigure realize_figure(const FigureTemplate& ft, StrategyContext& ctx) override;
};

inline MelodicFigure ShapeSuspensionStrategy::realize_figure(
    const FigureTemplate& ft, StrategyContext& ctx) {
  uint32_t seed = ft.seed ? ft.seed : ctx.rng->rng();
  FigureBuilder fb(seed);
  fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  return fb.suspension(fb.defaultPulse * 2, fb.defaultPulse);
}

// ====== ShapeCambiataStrategy ===============================================
class ShapeCambiataStrategy : public Strategy {
public:
  std::string name() const override { return "shape_cambiata"; }
  StrategyLevel level() const override { return StrategyLevel::Figure; }
  MelodicFigure realize_figure(const FigureTemplate& ft, StrategyContext& ctx) override;
};

inline MelodicFigure ShapeCambiataStrategy::realize_figure(
    const FigureTemplate& ft, StrategyContext& ctx) {
  uint32_t seed = ft.seed ? ft.seed : ctx.rng->rng();
  FigureBuilder fb(seed);
  fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  int dir = ft.shapeDirection;
  return fb.cambiata(dir, fb.defaultPulse);
}

// ====== ShapeSkippingStrategy ===============================================
class ShapeSkippingStrategy : public Strategy {
public:
  std::string name() const override { return "shape_skipping"; }
  StrategyLevel level() const override { return StrategyLevel::Figure; }
  MelodicFigure realize_figure(const FigureTemplate& ft, StrategyContext& ctx) override;
};

inline MelodicFigure ShapeSkippingStrategy::realize_figure(
    const FigureTemplate& ft, StrategyContext& ctx) {
  uint32_t seed = ft.seed ? ft.seed : ctx.rng->rng();
  Randomizer rng(seed);

  float totalBeats = (ft.totalBeats > 0) ? ft.totalBeats : 4.0f;
  float pulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;

  // Generate random musical rhythm
  auto rhythm = generate_musical_rhythm(totalBeats, pulse, rng);
  int noteCount = int(rhythm.size());
  if (noteCount == 0) return MelodicFigure{};

  MelodicFigure fig;
  int totalSteps = noteCount - 1;
  for (int i = 0; i < noteCount; ++i) {
    FigureUnit u;
    u.duration = rhythm[i];
    if (i == 0) {
      u.step = 0;  // first note at cursor
    } else {
      int sign = direction_sign(ft.direction, i - 1, totalSteps, rng);
      int magnitude = rng.decide(0.5f) ? 2 : 3;  // thirds or fourths
      u.step = sign * magnitude;
    }
    fig.units.push_back(u);
  }

  // Adjust last step to hit targetNet if specified
  if (ft.targetNet != 0 && noteCount > 1) {
    int currentNet = fig.net_step();
    int diff = ft.targetNet - currentNet;
    fig.units.back().step += diff;
  }

  return fig;
}

// ====== ShapeSteppingStrategy ===============================================
class ShapeSteppingStrategy : public Strategy {
public:
  std::string name() const override { return "shape_stepping"; }
  StrategyLevel level() const override { return StrategyLevel::Figure; }
  MelodicFigure realize_figure(const FigureTemplate& ft, StrategyContext& ctx) override;
};

inline MelodicFigure ShapeSteppingStrategy::realize_figure(
    const FigureTemplate& ft, StrategyContext& ctx) {
  uint32_t seed = ft.seed ? ft.seed : ctx.rng->rng();
  Randomizer rng(seed);

  float totalBeats = (ft.totalBeats > 0) ? ft.totalBeats : 4.0f;
  float pulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;

  auto rhythm = generate_musical_rhythm(totalBeats, pulse, rng);
  int noteCount = int(rhythm.size());
  if (noteCount == 0) return MelodicFigure{};

  MelodicFigure fig;
  int totalSteps = noteCount - 1;
  for (int i = 0; i < noteCount; ++i) {
    FigureUnit u;
    u.duration = rhythm[i];
    if (i == 0) {
      u.step = 0;
    } else {
      int sign = direction_sign(ft.direction, i - 1, totalSteps, rng);
      u.step = sign * 1;  // always stepwise (seconds)
    }
    fig.units.push_back(u);
  }

  // Adjust last step to hit targetNet if specified
  if (ft.targetNet != 0 && noteCount > 1) {
    int currentNet = fig.net_step();
    int diff = ft.targetNet - currentNet;
    fig.units.back().step += diff;
  }

  return fig;
}

} // namespace mforce
