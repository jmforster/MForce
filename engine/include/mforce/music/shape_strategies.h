#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/figures.h"
#include "mforce/music/templates.h"
#include "mforce/core/randomizer.h"
#include <algorithm>

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
  FigureBuilder fb(seed);
  fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  int dir = ft.shapeDirection;
  int p1 = ft.shapeParam;
  return fb.cadential_approach(dir < 0, p1 > 0 ? p1 : 3,
                               fb.defaultPulse * 2, fb.defaultPulse);
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

} // namespace mforce
