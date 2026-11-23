#pragma once
#include "mforce/music/pitch_reader.h"
#include "mforce/compose/piece_utils.h"
#include "mforce/compose/strategy.h"
#include "mforce/music/figures.h"
#include "mforce/compose/templates.h"
#include "mforce/compose/rhythm_util.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <cmath>

namespace mforce {

// ---------------------------------------------------------------------------
// Shape strategies — FigureStrategy subclasses for shape-specific logic that
// can't be expressed as a pure ordinal template.
//
// A shape that is only an ordinal template is a shape_figures:: template,
// called directly (or via RandomFigureBuilder's weighted dispatcher).
//
// The three below do real work: harmony-aware cadential
// targeting, literal-scale-step walking for Alberti/Skipping patterns,
// and motif rhythm/contour resolution. Their compose_figure bodies follow
// the classes, below.
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
  MelodicFigure compose_figure(Locus, const FigureTemplate& ft) const override;
};

// ====== ShapeSkippingStrategy ===============================================
class ShapeSkippingStrategy : public FigureStrategy {
public:
  static constexpr const char* kName = "shape_skipping";
  using Settings = NoSettings;

  std::string name() const override { return kName; }
  MelodicFigure compose_figure(Locus, const FigureTemplate& ft) const override;
};

// ====== ShapeSteppingStrategy ===============================================
class ShapeSteppingStrategy : public FigureStrategy {
public:
  static constexpr const char* kName = "shape_stepping";
  using Settings = NoSettings;

  std::string name() const override { return kName; }
  MelodicFigure compose_figure(Locus, const FigureTemplate& ft) const override;
};

// ============================================================================
// Out-of-line definitions for ShapeCadentialApproachStrategy,
// ShapeSkippingStrategy, and ShapeSteppingStrategy. They resolve motif
// references through Locus::plan and draw seeds from Locus::rng.
// ============================================================================

namespace detail {

inline PulseSequence resolve_rhythm(const FigureTemplate& ft, Locus locus,
                                     uint32_t seed, float totalBeats, float defaultPulse) {
  if (!ft.rhythmMotifName.empty()) {
    const PulseSequence* ps = locus.plan->find_rhythm_motif(ft.rhythmMotifName);
    if (ps) {
      PulseSequence result = *ps;
      if (ft.rhythmTransform == "retrograde") result = result.retrograded();
      else if (ft.rhythmTransform == "stretch")
        result = result.stretched(ft.rhythmTransformParam > 0 ? ft.rhythmTransformParam : 2.0f);
      else if (ft.rhythmTransform == "compress")
        result = result.compressed(ft.rhythmTransformParam > 0 ? ft.rhythmTransformParam : 2.0f);
      return result;
    }
  }
  PulseGenerator pgen(seed + 50);
  return pgen.generate(totalBeats, defaultPulse);
}

inline StepSequence resolve_contour(const FigureTemplate& ft, Locus locus,
                                     uint32_t seed, int noteCount, FigureDirection dir) {
  if (!ft.contourMotifName.empty()) {
    const StepSequence* ss = locus.plan->find_contour_motif(ft.contourMotifName);
    if (ss) {
      StepSequence result = *ss;
      if (ft.contourTransform == "invert") result = result.inverted();
      else if (ft.contourTransform == "retrograde") result = result.retrograded();
      else if (ft.contourTransform == "expand")
        result = result.expanded(ft.contourTransformParam > 0 ? ft.contourTransformParam : 2.0f);
      else if (ft.contourTransform == "contract")
        result = result.contracted(ft.contourTransformParam > 0 ? ft.contourTransformParam : 2.0f);
      return result;
    }
  }
  // No motif reference or not found — generate using direction
  StepSequence ss;
  Randomizer stepRng(seed);
  int totalSteps = noteCount;
  for (int i = 0; i < noteCount; ++i) {
    int sign = direction_sign(dir, i, totalSteps, stepRng);
    ss.add(sign);  // magnitude 1 for stepping; caller can scale for skipping
  }
  return ss;
}

} // namespace detail

inline MelodicFigure ShapeCadentialApproachStrategy::compose_figure(
    Locus locus, const FigureTemplate& ft) const {
  uint32_t seed = ft.seed ? ft.seed : locus.rng->rng();
  Randomizer rng(seed);

  const Scale& scale = locus.piece->sections[locus.sectionIdx].scale;
  const Pitch cursor = ::mforce::piece_utils::pitch_before(locus);

  float totalBeats = (ft.totalBeats > 0) ? ft.totalBeats : 4.0f;
  float pulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;

  PulseSequence rhythm = detail::resolve_rhythm(ft, locus, seed, totalBeats, pulse);

  // Functional coupling: ensure last note is at least double the average
  if (rhythm.count() >= 2) {
    float avg = rhythm.total_length() / rhythm.count();
    if (rhythm.pulses.back() < avg * 1.5f) {
      float steal = avg;
      rhythm.pulses.back() += steal;
      int donors = rhythm.count() - 1;
      if (donors > 0) {
        float perDonor = steal / donors;
        for (int i = 0; i < donors; ++i) {
          rhythm.pulses[i] = std::max(0.125f, rhythm.pulses[i] - perDonor);
        }
      }
    }
  }

  int noteCount = rhythm.count();
  if (noteCount == 0) return MelodicFigure{};

  // --- Determine target pitch ---
  // If figureCadenceType is set and we have harmony context, derive target from chords.
  // 1 = half cadence (target V), 2 = full cadence (target I).
  // perfect = true → root of target chord, false → 3rd or 5th.
  // Fallback: use targetNet / shapeParam as before.

  int targetSteps = 0;
  bool targetDerived = false;

  if (ft.figureCadenceType > 0) {
    // Harmony-aware cadential targeting.
    // half (1) = target V, so descend to scale degree 4 (G in C major)
    // full (2) = target I, so descend to scale degree 0 (C)
    //
    // We express the target as a scale-degree offset from the tonic.
    // half cadence: degree 4 is 4 steps above tonic, but we want to descend
    //   to it — so net movement = -(scale_length - 4) = -3 in a 7-note scale.
    // full cadence perfect: land on tonic = 0 net from tonic. From wherever
    //   the cursor is, we want net = -cursor_degree (descend to root).
    //
    // Simple approach: half cadence = net -3 (descend C→G in major),
    //   full cadence = net -7 (descend to tonic one octave down... too far).
    //
    // Actually: just use the interval. In a major scale, V is 4 degrees up
    // from I. A half cadence typically descends to V, so from above:
    // if we're roughly around the tonic register, we descend ~3 steps to V.
    // A full cadence descends to I — if we're near V, that's ~4 steps down;
    // if we're near the upper tonic, ~7 steps down.
    //
    // Simple and direct:
    int targetDegree = (ft.figureCadenceType == 1) ? 4 : 0;  // V or I

    // Calculate how many scale steps from the cursor's approximate degree
    // to the target degree. Prefer descending approach (more natural for cadences).
    // We don't know the cursor's exact degree, but we can estimate.
    PitchReader pr(scale);
    pr.set_pitch(cursor);
    int curDeg = pr.get_degree();

    int descending = curDeg - targetDegree;
    if (descending <= 0) descending += scale.length();

    int ascending = targetDegree - curDeg;
    if (ascending <= 0) ascending += scale.length();

    // Prefer descending for cadences unless ascending is much shorter
    if (descending <= ascending + 2) {
      targetSteps = -descending;
    } else {
      targetSteps = ascending;
    }

    // For imperfect cadences, land on the 3rd of the target chord.
    // Half cadence: 3rd of V = leading tone (max tension, demands resolution).
    // Full cadence: 3rd of I = stable but not final (saves the PAC for later).
    if (!ft.perfect) {
      targetSteps += 2;  // 3rd is 2 scale degrees above root
    }

    targetDerived = true;
  }

  if (!targetDerived) {
    // Fallback: use targetNet or shapeParam
    int approachDir = (ft.shapeDirection < 0) ? -1 : 1;
    targetSteps = (ft.targetNet != 0) ? ft.targetNet :
                  ((ft.shapeParam > 0) ? -ft.shapeParam * approachDir : -3 * approachDir);
  }

  // Minimum approach distance: if the target is too close, go away first
  // then come back. This creates the "arch" shape of a real cadential approach
  // (e.g., Mozart K467 bar 4: can't go straight to target, so goes up then down).
  //
  // Two-phase contour: optional departure (opposite direction), then approach.
  // Net movement is always targetSteps — the departure is round-trip overhead.
  int minMotion = noteCount - 1;  // at most 1 note of arrival hold
  if (minMotion < 3) minMotion = 3;
  int absTarget = std::abs(targetSteps);
  int departureCount = 0;
  int departDir = 0;
  if (absTarget < minMotion && noteCount >= 4) {
    departureCount = (minMotion - absTarget + 1) / 2;  // how many notes going away
    departDir = (targetSteps <= 0) ? 1 : -1;           // opposite to final target
  }

  // Generate two-phase contour
  StepSequence contour;
  // Phase 1: departure (go away from target)
  for (int i = 0; i < departureCount; ++i) {
    contour.add(departDir);
  }
  // Phase 2: approach toward target (net = targetSteps, undoing departure)
  int approachTarget = targetSteps - departureCount * departDir;  // total steps for approach phase
  int stepsRemaining = approachTarget;
  int approachNotes = noteCount - departureCount;
  for (int i = 0; i < approachNotes; ++i) {
    if (i == approachNotes - 1) {
      contour.add(stepsRemaining);  // arrival: whatever's left
    } else if (stepsRemaining != 0) {
      int s = (stepsRemaining > 0) ? 1 : -1;
      contour.add(s);
      stepsRemaining -= s;
    } else {
      contour.add(0);
    }
  }

  // If a contour motif was provided, use it instead (override the generated one)
  if (!ft.contourMotifName.empty()) {
    const StepSequence* ss = locus.plan->find_contour_motif(ft.contourMotifName);
    if (ss) {
      contour = *ss;
      if (ft.contourTransform == "invert") contour = contour.inverted();
      else if (ft.contourTransform == "retrograde") contour = contour.retrograded();
      else if (ft.contourTransform == "expand")
        contour = contour.expanded(ft.contourTransformParam > 0 ? ft.contourTransformParam : 2.0f);
      else if (ft.contourTransform == "contract")
        contour = contour.contracted(ft.contourTransformParam > 0 ? ft.contourTransformParam : 2.0f);
      if (ft.targetNet != 0 && contour.count() > 1) {
        int diff = ft.targetNet - contour.net();
        contour.steps.back() += diff;
      }
    }
  }

  while (contour.count() < rhythm.count()) contour.add(0);
  while (contour.count() > rhythm.count()) contour.steps.pop_back();

  return MelodicFigure(rhythm, contour);
}

inline MelodicFigure ShapeSkippingStrategy::compose_figure(
    Locus locus, const FigureTemplate& ft) const {
  uint32_t seed = ft.seed ? ft.seed : locus.rng->rng();
  Randomizer rng(seed);
  float totalBeats = (ft.totalBeats > 0) ? ft.totalBeats : 4.0f;
  float pulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;

  PulseSequence rhythm = detail::resolve_rhythm(ft, locus, seed, totalBeats, pulse);
  int noteCount = rhythm.count();
  if (noteCount == 0) return MelodicFigure{};

  StepSequence contour = detail::resolve_contour(ft, locus, seed, noteCount, ft.direction);

  // Scale contour magnitudes for skipping (thirds/fourths)
  // If contour came from a motif, its magnitudes are already set — don't rescale.
  if (ft.contourMotifName.empty()) {
    Randomizer magRng(seed + 77);
    for (int i = 0; i < contour.count(); ++i) {
      if (contour.steps[i] != 0) {
        int sign = (contour.steps[i] > 0) ? 1 : -1;
        int mag = magRng.decide(0.5f) ? 2 : 3;
        contour.steps[i] = sign * mag;
      }
    }
  }

  // Adjust for targetNet
  if (ft.targetNet != 0 && contour.count() > 1) {
    int diff = ft.targetNet - contour.net();
    contour.steps.back() += diff;
  }

  // Pad/trim contour to match rhythm length
  while (contour.count() < rhythm.count()) contour.add(0);
  while (contour.count() > rhythm.count()) contour.steps.pop_back();

  return MelodicFigure(rhythm, contour);
}

inline MelodicFigure ShapeSteppingStrategy::compose_figure(
    Locus locus, const FigureTemplate& ft) const {
  uint32_t seed = ft.seed ? ft.seed : locus.rng->rng();
  float totalBeats = (ft.totalBeats > 0) ? ft.totalBeats : 4.0f;
  float pulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;

  PulseSequence rhythm = detail::resolve_rhythm(ft, locus, seed, totalBeats, pulse);
  int noteCount = rhythm.count();
  if (noteCount == 0) return MelodicFigure{};

  StepSequence contour = detail::resolve_contour(ft, locus, seed, noteCount, ft.direction);

  if (ft.targetNet != 0 && contour.count() > 1) {
    int diff = ft.targetNet - contour.net();
    contour.steps.back() += diff;
  }

  while (contour.count() < rhythm.count()) contour.add(0);
  while (contour.count() > rhythm.count()) contour.steps.pop_back();

  return MelodicFigure(rhythm, contour);
}

} // namespace mforce
