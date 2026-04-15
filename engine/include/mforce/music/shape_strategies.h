#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/figures.h"
#include "mforce/music/templates.h"
#include "mforce/music/rhythm_util.h"
#include "mforce/music/rng.h"
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
class ShapeScalarRunStrategy : public FigureStrategy {
public:
  std::string name() const override { return "shape_scalar_run"; }
  MelodicFigure realize_figure(Locus, const FigureTemplate& ft) override;
};

inline MelodicFigure ShapeScalarRunStrategy::realize_figure(
    Locus, const FigureTemplate& ft) {
  uint32_t seed = ft.seed ? ft.seed : ::mforce::rng::next();
  FigureBuilder fb(seed);
  fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  int dir = ft.shapeDirection;
  int count = (ft.maxNotes > ft.minNotes)
      ? Randomizer(seed + 99).int_range(ft.minNotes, ft.maxNotes)
      : (ft.minNotes > 0 ? ft.minNotes : 4);
  return fb.scalar_run(dir, count > 0 ? count : 4, fb.defaultPulse);
}

// ====== ShapeRepeatedNoteStrategy ===========================================
class ShapeRepeatedNoteStrategy : public FigureStrategy {
public:
  std::string name() const override { return "shape_repeated_note"; }
  MelodicFigure realize_figure(Locus, const FigureTemplate& ft) override;
};

inline MelodicFigure ShapeRepeatedNoteStrategy::realize_figure(
    Locus, const FigureTemplate& ft) {
  uint32_t seed = ft.seed ? ft.seed : ::mforce::rng::next();
  FigureBuilder fb(seed);
  fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  int count = (ft.maxNotes > ft.minNotes)
      ? Randomizer(seed + 99).int_range(ft.minNotes, ft.maxNotes)
      : (ft.minNotes > 0 ? ft.minNotes : 4);
  return fb.repeated_note(count > 0 ? count : 3, fb.defaultPulse);
}

// ====== ShapeHeldNoteStrategy ===============================================
class ShapeHeldNoteStrategy : public FigureStrategy {
public:
  std::string name() const override { return "shape_held_note"; }
  MelodicFigure realize_figure(Locus, const FigureTemplate& ft) override;
};

inline MelodicFigure ShapeHeldNoteStrategy::realize_figure(
    Locus, const FigureTemplate& ft) {
  uint32_t seed = ft.seed ? ft.seed : ::mforce::rng::next();
  FigureBuilder fb(seed);
  fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  return fb.held_note(ft.totalBeats > 0 ? ft.totalBeats : fb.defaultPulse * 2);
}

// ====== ShapeCadentialApproachStrategy ======================================
class ShapeCadentialApproachStrategy : public FigureStrategy {
public:
  std::string name() const override { return "shape_cadential_approach"; }
  MelodicFigure realize_figure(Locus, const FigureTemplate& ft) override;
};

// ====== ShapeTriadicOutlineStrategy =========================================
class ShapeTriadicOutlineStrategy : public FigureStrategy {
public:
  std::string name() const override { return "shape_triadic_outline"; }
  MelodicFigure realize_figure(Locus, const FigureTemplate& ft) override;
};

inline MelodicFigure ShapeTriadicOutlineStrategy::realize_figure(
    Locus, const FigureTemplate& ft) {
  uint32_t seed = ft.seed ? ft.seed : ::mforce::rng::next();
  FigureBuilder fb(seed);
  fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  int dir = ft.shapeDirection;
  int p1 = ft.shapeParam;
  return fb.triadic_outline(dir, p1 > 0, fb.defaultPulse);
}

// ====== ShapeNeighborToneStrategy ===========================================
class ShapeNeighborToneStrategy : public FigureStrategy {
public:
  std::string name() const override { return "shape_neighbor_tone"; }
  MelodicFigure realize_figure(Locus, const FigureTemplate& ft) override;
};

inline MelodicFigure ShapeNeighborToneStrategy::realize_figure(
    Locus, const FigureTemplate& ft) {
  uint32_t seed = ft.seed ? ft.seed : ::mforce::rng::next();
  FigureBuilder fb(seed);
  fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  int dir = ft.shapeDirection;
  return fb.neighbor_tone(dir > 0, fb.defaultPulse);
}

// ====== ShapeLeapAndFillStrategy ============================================
class ShapeLeapAndFillStrategy : public FigureStrategy {
public:
  std::string name() const override { return "shape_leap_and_fill"; }
  MelodicFigure realize_figure(Locus, const FigureTemplate& ft) override;
};

inline MelodicFigure ShapeLeapAndFillStrategy::realize_figure(
    Locus, const FigureTemplate& ft) {
  uint32_t seed = ft.seed ? ft.seed : ::mforce::rng::next();
  FigureBuilder fb(seed);
  fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  int dir = ft.shapeDirection;
  int p1 = ft.shapeParam;
  int p2 = ft.shapeParam2;
  return fb.leap_and_fill(p1 > 0 ? p1 : 4, dir > 0, p2, fb.defaultPulse);
}

// ====== ShapeScalarReturnStrategy ===========================================
class ShapeScalarReturnStrategy : public FigureStrategy {
public:
  std::string name() const override { return "shape_scalar_return"; }
  MelodicFigure realize_figure(Locus, const FigureTemplate& ft) override;
};

inline MelodicFigure ShapeScalarReturnStrategy::realize_figure(
    Locus, const FigureTemplate& ft) {
  uint32_t seed = ft.seed ? ft.seed : ::mforce::rng::next();
  FigureBuilder fb(seed);
  fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  int dir = ft.shapeDirection;
  int p1 = ft.shapeParam;
  int p2 = ft.shapeParam2;
  return fb.scalar_return(dir, p1 > 0 ? p1 : 3, p2, fb.defaultPulse);
}

// ====== ShapeAnacrusisStrategy ==============================================
class ShapeAnacrusisStrategy : public FigureStrategy {
public:
  std::string name() const override { return "shape_anacrusis"; }
  MelodicFigure realize_figure(Locus, const FigureTemplate& ft) override;
};

inline MelodicFigure ShapeAnacrusisStrategy::realize_figure(
    Locus, const FigureTemplate& ft) {
  uint32_t seed = ft.seed ? ft.seed : ::mforce::rng::next();
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
class ShapeZigzagStrategy : public FigureStrategy {
public:
  std::string name() const override { return "shape_zigzag"; }
  MelodicFigure realize_figure(Locus, const FigureTemplate& ft) override;
};

inline MelodicFigure ShapeZigzagStrategy::realize_figure(
    Locus, const FigureTemplate& ft) {
  uint32_t seed = ft.seed ? ft.seed : ::mforce::rng::next();
  FigureBuilder fb(seed);
  fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  int dir = ft.shapeDirection;
  int p1 = ft.shapeParam;
  return fb.zigzag(dir, p1 > 0 ? p1 : 3, 2, 1, fb.defaultPulse);
}

// ====== ShapeFanfareStrategy ================================================
class ShapeFanfareStrategy : public FigureStrategy {
public:
  std::string name() const override { return "shape_fanfare"; }
  MelodicFigure realize_figure(Locus, const FigureTemplate& ft) override;
};

inline MelodicFigure ShapeFanfareStrategy::realize_figure(
    Locus, const FigureTemplate& ft) {
  uint32_t seed = ft.seed ? ft.seed : ::mforce::rng::next();
  FigureBuilder fb(seed);
  fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  int p1 = ft.shapeParam;
  return fb.fanfare({4, 3}, p1 > 0 ? p1 : 1, fb.defaultPulse);
}

// ====== ShapeSighStrategy ===================================================
class ShapeSighStrategy : public FigureStrategy {
public:
  std::string name() const override { return "shape_sigh"; }
  MelodicFigure realize_figure(Locus, const FigureTemplate& ft) override;
};

inline MelodicFigure ShapeSighStrategy::realize_figure(
    Locus, const FigureTemplate& ft) {
  uint32_t seed = ft.seed ? ft.seed : ::mforce::rng::next();
  FigureBuilder fb(seed);
  fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  return fb.sigh(fb.defaultPulse);
}

// ====== ShapeSuspensionStrategy =============================================
class ShapeSuspensionStrategy : public FigureStrategy {
public:
  std::string name() const override { return "shape_suspension"; }
  MelodicFigure realize_figure(Locus, const FigureTemplate& ft) override;
};

inline MelodicFigure ShapeSuspensionStrategy::realize_figure(
    Locus, const FigureTemplate& ft) {
  uint32_t seed = ft.seed ? ft.seed : ::mforce::rng::next();
  FigureBuilder fb(seed);
  fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  return fb.suspension(fb.defaultPulse * 2, fb.defaultPulse);
}

// ====== ShapeCambiataStrategy ===============================================
class ShapeCambiataStrategy : public FigureStrategy {
public:
  std::string name() const override { return "shape_cambiata"; }
  MelodicFigure realize_figure(Locus, const FigureTemplate& ft) override;
};

inline MelodicFigure ShapeCambiataStrategy::realize_figure(
    Locus, const FigureTemplate& ft) {
  uint32_t seed = ft.seed ? ft.seed : ::mforce::rng::next();
  FigureBuilder fb(seed);
  fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  int dir = ft.shapeDirection;
  return fb.cambiata(dir, fb.defaultPulse);
}

// ====== ShapeSkippingStrategy ===============================================
class ShapeSkippingStrategy : public FigureStrategy {
public:
  std::string name() const override { return "shape_skipping"; }
  MelodicFigure realize_figure(Locus, const FigureTemplate& ft) override;
};

// ====== ShapeSteppingStrategy ===============================================
class ShapeSteppingStrategy : public FigureStrategy {
public:
  std::string name() const override { return "shape_stepping"; }
  MelodicFigure realize_figure(Locus, const FigureTemplate& ft) override;
};

} // namespace mforce
