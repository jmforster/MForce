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
// Shape strategies — FigureStrategy subclasses for shape-specific logic that
// can't be expressed as a pure ordinal template.
//
// The 13 wrapper strategies that used to live here (ShapeScalarRunStrategy,
// ShapeRepeatedNoteStrategy, etc.) were deleted in the FigureBuilder
// redesign — their bodies were thin wrappers around FigureBuilder atoms
// and are now better expressed via shape_figures:: templates called
// directly (or via RandomFigureBuilder's weighted dispatcher).
//
// The three survivors below do real work: harmony-aware cadential
// targeting, literal-scale-step walking for Alberti/Skipping patterns,
// and motif rhythm/contour resolution. Their compose_figure bodies live
// out-of-line in composer.h.
//
// The default figure strategy finds them in the kit by name:
//   "shape_cadential_approach", "shape_skipping", "shape_stepping".
// ---------------------------------------------------------------------------

// ====== ShapeCadentialApproachStrategy ======================================
class ShapeCadentialApproachStrategy : public FigureStrategy {
public:
  static constexpr const char* kName = "shape_cadential_approach";
  using Settings = NoSettings;

  std::string name() const override { return kName; }
  MelodicFigure compose_figure(Locus, const FigureTemplate& ft) override;
};

// ====== ShapeSkippingStrategy ===============================================
class ShapeSkippingStrategy : public FigureStrategy {
public:
  static constexpr const char* kName = "shape_skipping";
  using Settings = NoSettings;

  std::string name() const override { return kName; }
  MelodicFigure compose_figure(Locus, const FigureTemplate& ft) override;
};

// ====== ShapeSteppingStrategy ===============================================
class ShapeSteppingStrategy : public FigureStrategy {
public:
  static constexpr const char* kName = "shape_stepping";
  using Settings = NoSettings;

  std::string name() const override { return kName; }
  MelodicFigure compose_figure(Locus, const FigureTemplate& ft) override;
};

} // namespace mforce
