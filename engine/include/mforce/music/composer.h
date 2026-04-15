#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/strategy_registry.h"
#include "mforce/music/default_strategies.h"
#include "mforce/music/shape_strategies.h"
#include "mforce/music/phrase_strategies.h"
#include "mforce/music/alternating_figure_strategy.h"
#include "mforce/music/harmony_composer.h"
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"
#include "mforce/music/pitch_reader.h"
#include "mforce/music/rng.h"
#include "mforce/core/randomizer.h"
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>

namespace mforce {

// ---------------------------------------------------------------------------
// Composer
//
// Owns the StrategyRegistry, the master RNG, and the realized-seed pool.
// Walks a PieceTemplate and dispatches to the registered strategies at each
// level of the hierarchy.
//
// Phase 1a: the only strategies registered are the three Defaults, and
// dispatch always selects them regardless of any `strategy` field on the
// template. Template-driven strategy selection arrives in Phase 3.
// ---------------------------------------------------------------------------
struct Composer {
  // Note on seed + 200: the pre-refactor ClassicalComposer had three
  // Randomizer-like instance members seeded with (seed, seed+100, seed+200).
  // The rng member — the one used for all per-figure/per-phrase random
  // decisions — was seeded with seed+200. Refactor dropped the other two
  // (dead code in practice), but rng_ must preserve the seed+200 offset
  // or every RNG call consumes a different stream and the composer stops
  // being bit-identical to the pre-refactor output.
  explicit Composer(uint32_t seed = 0xC1A5'0001u) : rng_(seed + 200) {
    registry_.register_strategy(std::make_unique<DefaultFigureStrategy>());
    registry_.register_strategy(std::make_unique<DefaultPhraseStrategy>());
    registry_.register_strategy(std::make_unique<DefaultPassageStrategy>());

    // Shape strategies (Phase 2)
    registry_.register_strategy(std::make_unique<ShapeScalarRunStrategy>());
    registry_.register_strategy(std::make_unique<ShapeRepeatedNoteStrategy>());
    registry_.register_strategy(std::make_unique<ShapeHeldNoteStrategy>());
    registry_.register_strategy(std::make_unique<ShapeCadentialApproachStrategy>());
    registry_.register_strategy(std::make_unique<ShapeTriadicOutlineStrategy>());
    registry_.register_strategy(std::make_unique<ShapeNeighborToneStrategy>());
    registry_.register_strategy(std::make_unique<ShapeLeapAndFillStrategy>());
    registry_.register_strategy(std::make_unique<ShapeScalarReturnStrategy>());
    registry_.register_strategy(std::make_unique<ShapeAnacrusisStrategy>());
    registry_.register_strategy(std::make_unique<ShapeZigzagStrategy>());
    registry_.register_strategy(std::make_unique<ShapeFanfareStrategy>());
    registry_.register_strategy(std::make_unique<ShapeSighStrategy>());
    registry_.register_strategy(std::make_unique<ShapeSuspensionStrategy>());
    registry_.register_strategy(std::make_unique<ShapeCambiataStrategy>());
    registry_.register_strategy(std::make_unique<ShapeSkippingStrategy>());
    registry_.register_strategy(std::make_unique<ShapeSteppingStrategy>());

    // Phrase strategies (Phase 3)
    registry_.register_strategy(std::make_unique<PeriodPhraseStrategy>());
    registry_.register_strategy(std::make_unique<SentencePhraseStrategy>());

    // Passage strategies (Task 7)
    registry_.register_strategy(std::make_unique<AlternatingFigureStrategy>());
  }

  // --- Top-level composition ---
  void compose(Piece& piece, const PieceTemplate& tmpl) {
    setup_piece_(piece, tmpl);
    for (const auto& partTmpl : tmpl.parts) {
      for (auto& sec : piece.sections) {
        compose_passage_(piece, tmpl, partTmpl, sec.name);
      }
    }
  }

  // --- Dispatchers called from strategies ---
  MelodicFigure realize_figure(const FigureTemplate& figTmpl,
                               StrategyContext& ctx) {
    Strategy* s = registry_.get("default_figure");
    return s->realize_figure(figTmpl, ctx);
  }

  Phrase realize_phrase(const PhraseTemplate& phraseTmpl,
                        StrategyContext& ctx) {
    std::string n = phraseTmpl.strategy.empty() ? std::string("default_phrase") : phraseTmpl.strategy;
    Strategy* s = registry_.get(n);
    if (!s) {
      std::cerr << "Unknown phrase strategy '" << n << "', falling back to default_phrase\n";
      s = registry_.get("default_phrase");
    }
    return s->realize_phrase(phraseTmpl, ctx);
  }

  Passage realize_passage(const PassageTemplate& passTmpl,
                          StrategyContext& ctx) {
    std::string n = passTmpl.strategy.empty() ? std::string("default_passage") : passTmpl.strategy;
    Strategy* s = registry_.get(n);
    if (!s) {
      std::cerr << "Unknown passage strategy '" << n << "', falling back to default_passage\n";
      s = registry_.get("default_passage");
    }
    return s->realize_passage(passTmpl, ctx);
  }

  // --- Accessor for DefaultFigureStrategy::realize_figure ---
  const std::unordered_map<std::string, MelodicFigure>& realized_motifs() const {
    return realizedMotifs_;
  }

  const PulseSequence* find_rhythm_motif(const std::string& name) const {
    auto it = realizedRhythms_.find(name);
    return it == realizedRhythms_.end() ? nullptr : &it->second;
  }

  const StepSequence* find_contour_motif(const std::string& name) const {
    auto it = realizedContours_.find(name);
    return it == realizedContours_.end() ? nullptr : &it->second;
  }

  // --- Public registry lookup for Phase 2 shape dispatch ---
  // Named awkwardly to signal it exists specifically for the shape-dispatch
  // path in DefaultFigureStrategy::realize_figure. May be renamed later.
  Strategy* registry_get_for_phase2(const std::string& name) const {
    return registry_.get(name);
  }

  // --- Public helper used by the IComposer-compatible ClassicalComposer
  // --- facade in Task 8. Given a (part, section) pair, realize exactly that
  // --- passage onto piece.parts[].passages[section]. Not called during the
  // --- normal compose() walk.
  void compose_one_passage(Piece& piece, const PieceTemplate& tmpl,
                           const std::string& partName,
                           const std::string& sectionName) {
    const PartTemplate* partTmpl = nullptr;
    for (auto& pt : tmpl.parts) {
      if (pt.name == partName) { partTmpl = &pt; break; }
    }
    if (!partTmpl) return;
    compose_passage_(piece, tmpl, *partTmpl, sectionName);
  }

private:
  Randomizer rng_;
  StrategyRegistry registry_;
  std::unordered_map<std::string, MelodicFigure> realizedMotifs_;
  std::unordered_map<std::string, PulseSequence> realizedRhythms_;
  std::unordered_map<std::string, StepSequence> realizedContours_;

  // ---- Pre-refactor port: ClassicalComposer::setup_piece -----------------
  void setup_piece_(Piece& piece, const PieceTemplate& tmpl) {
    // Key
    piece.key = Key::get(tmpl.keyName + " " +
        (tmpl.scaleName == "Minor" ? "Minor" : "Major"));

    // Sections
    piece.sections.clear();
    for (auto& sd : tmpl.sections) {
      Scale secScale = sd.scaleOverride.empty()
        ? piece.key.scale
        : Scale::get(tmpl.keyName, sd.scaleOverride);
      piece.add_section(Section(sd.name, sd.beats, tmpl.bpm, tmpl.meter, secScale));

      // Wire harmony
      Section& section = piece.sections.back();
      if (sd.chordProgression) {
        section.chordProgression = *sd.chordProgression;
      } else if (!sd.progressionName.empty()) {
        section.chordProgression = HarmonyComposer::build(sd.progressionName, sd.beats);
      }
      section.keyContexts = sd.keyContexts;
    }

    // Parts
    piece.parts.clear();
    for (auto& pt : tmpl.parts) {
      Part p;
      p.name = pt.name;
      p.instrumentType = pt.name;  // conductor looks up by instrumentType
      piece.parts.push_back(std::move(p));
    }

    // Realize motifs
    realize_motifs_(piece, tmpl);
  }

  // ---- Pre-refactor port: ClassicalComposer::realize_motifs ---------------
  void realize_motifs_(const Piece& /*piece*/, const PieceTemplate& tmpl) {
    realizedMotifs_.clear();
    realizedRhythms_.clear();
    realizedContours_.clear();

    // Pick a shared pulse for all generated motifs (phrase-level coherence).
    // Use piece-level default if specified, otherwise randomize once.
    float sharedPulse = tmpl.defaultPulse;
    if (sharedPulse <= 0) {
      Randomizer pulseRng(rng_.rng());
      static const float pulses[] = {0.5f, 0.5f, 1.0f, 1.0f, 1.0f, 1.5f, 2.0f};
      sharedPulse = pulses[pulseRng.int_range(0, 6)];
    }

    for (auto& motif : tmpl.motifs) {
      if (motif.is_rhythm()) {
        // PulseSequence motif
        if (motif.userProvided || !motif.rhythm().pulses.empty()) {
          realizedRhythms_[motif.name] = motif.rhythm();
        } else {
          uint32_t s = motif.generationSeed ? motif.generationSeed : rng_.rng();
          float totalBeats = 4.0f;
          if (motif.constraints && motif.constraints->totalBeats > 0)
            totalBeats = motif.constraints->totalBeats;
          PulseGenerator pgen(s);
          realizedRhythms_[motif.name] = pgen.generate(totalBeats, sharedPulse);
        }
      } else if (motif.is_contour()) {
        // StepSequence motif
        if (motif.userProvided || !motif.contour().steps.empty()) {
          realizedContours_[motif.name] = motif.contour();
        } else {
          uint32_t s = motif.generationSeed ? motif.generationSeed : rng_.rng();
          int length = 4;
          if (motif.constraints && motif.constraints->maxNotes > 0)
            length = motif.constraints->maxNotes;
          StepGenerator sgen(s);
          realizedContours_[motif.name] = sgen.random_sequence(length);
        }
      } else {
        // MelodicFigure motif (existing path, unchanged)
        if (motif.userProvided || !motif.figure().units.empty()) {
          realizedMotifs_[motif.name] = motif.figure();
        } else {
          uint32_t s = motif.generationSeed ? motif.generationSeed : rng_.rng();
          FigureTemplate ft = motif.constraints.value_or(FigureTemplate{});
          // Inherit shared pulse if the motif doesn't specify its own
          if (ft.defaultPulse <= 0) ft.defaultPulse = sharedPulse;
          DefaultFigureStrategy figStrat;
          realizedMotifs_[motif.name] = figStrat.generate_figure(ft, s);
        }
      }
    }
  }

  // ---- Pre-refactor port: ClassicalComposer::generate_default_passage -----
  Passage generate_default_passage_(const Piece& piece, const PieceTemplate& tmpl,
                                    const Scale& scale) {
    PitchReader reader(scale);
    reader.set_pitch(5, 0);
    Pitch startPitch = reader.get_pitch();

    // Simple binary form: antecedent + consequent
    uint32_t s1 = rng_.rng(), s2 = rng_.rng();

    FigureTemplate genFig;
    genFig.source = FigureSource::Generate;
    genFig.totalBeats = 4.0f;
    genFig.seed = s1;

    FigureTemplate genFig2;
    genFig2.source = FigureSource::Generate;
    genFig2.totalBeats = 4.0f;
    genFig2.seed = s2;

    FigureTemplate cadHalf;
    cadHalf.source = FigureSource::Generate;
    cadHalf.totalBeats = 2.0f;
    cadHalf.minNotes = 1;
    cadHalf.maxNotes = 1;
    cadHalf.seed = s1 + 100;

    FigureTemplate cadFull;
    cadFull.source = FigureSource::Generate;
    cadFull.totalBeats = 4.0f;
    cadFull.minNotes = 1;
    cadFull.maxNotes = 1;
    cadFull.seed = s2 + 100;

    PhraseTemplate ante;
    ante.name = "antecedent";
    ante.startingPitch = startPitch;
    ante.figures = {genFig, genFig2, cadHalf};

    PhraseTemplate cons;
    cons.name = "consequent";
    cons.startingPitch = startPitch;
    cons.figures = {genFig, genFig2, cadFull};

    PassageTemplate passTmpl;
    passTmpl.phrases = {ante, cons};

    StrategyContext ctx;
    ctx.scale = scale;
    ctx.piece = const_cast<Piece*>(&piece);
    ctx.template_ = &tmpl;
    ctx.composer = this;
    ctx.rng = &rng_;
    ::mforce::rng::Scope rngScope(rng_);
    return this->realize_passage(passTmpl, ctx);
  }

  // ---- Per-passage dispatcher: port of
  // ---- ClassicalComposer::compose(piece, tmpl, partName, sectionName)
  // ---- at classical_composer.h:49-77 -------------------------------------
  void compose_passage_(Piece& piece, const PieceTemplate& tmpl,
                        const PartTemplate& partTmpl,
                        const std::string& sectionName) {
    // Find the Part object in the piece to hold the realized passage.
    Part* part = nullptr;
    for (auto& p : piece.parts) {
      if (p.name == partTmpl.name) { part = &p; break; }
    }
    if (!part) return;

    // Find the passage template for this section on the part template.
    auto passIt = partTmpl.passages.find(sectionName);
    Scale scale = piece.key.scale;

    // Find the Section object for harmonic context.
    const Section* section = nullptr;
    for (const auto& s : piece.sections) {
      if (s.name == sectionName) { section = &s; break; }
    }

    Passage passage;
    if (passIt != partTmpl.passages.end()) {
      // Template-driven: dispatch through the registry.
      StrategyContext ctx;
      ctx.scale = scale;
      ctx.piece = &piece;
      ctx.template_ = &tmpl;
      ctx.composer = this;
      ctx.rng = &rng_;
      if (section) {
        ctx.chordProgression = section->chordProgression ? &*section->chordProgression : nullptr;
        ctx.keyContexts = section->keyContexts.empty() ? nullptr : &section->keyContexts;
      }
      ::mforce::rng::Scope rngScope(rng_);
      passage = realize_passage(passIt->second, ctx);
    } else {
      // No template for this section — fallback.
      passage = generate_default_passage_(piece, tmpl, scale);
    }

    part->passages[sectionName] = std::move(passage);
  }
};

// ============================================================================
// Out-of-line definitions for ShapeCadentialApproachStrategy,
// ShapeSkippingStrategy, and ShapeSteppingStrategy.
//
// These bodies live here — BELOW the Composer class — because they call
// ctx.composer->find_rhythm_motif() and ctx.composer->find_contour_motif(),
// which require the full Composer definition. Placing them in
// shape_strategies.h (where Composer is only forward-declared) would cause
// incomplete-type errors. Same pattern as DefaultFigureStrategy::realize_figure.
// ============================================================================

namespace detail {

inline PulseSequence resolve_rhythm(const FigureTemplate& ft, StrategyContext& ctx,
                                     uint32_t seed, float totalBeats, float defaultPulse) {
  if (!ft.rhythmMotifName.empty()) {
    const PulseSequence* ps = ctx.composer->find_rhythm_motif(ft.rhythmMotifName);
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

inline StepSequence resolve_contour(const FigureTemplate& ft, StrategyContext& ctx,
                                     uint32_t seed, int noteCount, FigureDirection dir) {
  if (!ft.contourMotifName.empty()) {
    const StepSequence* ss = ctx.composer->find_contour_motif(ft.contourMotifName);
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

inline MelodicFigure ShapeCadentialApproachStrategy::realize_figure(
    const FigureTemplate& ft, StrategyContext& ctx) {
  uint32_t seed = ft.seed ? ft.seed : ::mforce::rng::next();
  Randomizer rng(seed);

  float totalBeats = (ft.totalBeats > 0) ? ft.totalBeats : 4.0f;
  float pulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;

  // Resolve rhythm (from motif or generated)
  PulseSequence rhythm = detail::resolve_rhythm(ft, ctx, seed, totalBeats, pulse);

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
    // For now, keep it simple and direct:
    int targetDegree = (ft.figureCadenceType == 1) ? 4 : 0;  // V or I

    // Calculate how many scale steps from the cursor's approximate degree
    // to the target degree. Prefer descending approach (more natural for cadences).
    // We don't know the cursor's exact degree, but we can estimate.
    PitchReader pr(ctx.scale);
    pr.set_pitch(ctx.cursor);
    int curDeg = pr.get_degree();

    // How far down to the target degree (within one octave)?
    int descending = curDeg - targetDegree;
    if (descending <= 0) descending += ctx.scale.length();  // wrap: e.g., from deg 2 down to deg 4 = 5 steps in 7-note scale

    // How far up?
    int ascending = targetDegree - curDeg;
    if (ascending <= 0) ascending += ctx.scale.length();

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
    // Legacy fallback: use targetNet or shapeParam
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
    const StepSequence* ss = ctx.composer->find_contour_motif(ft.contourMotifName);
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

inline MelodicFigure ShapeSkippingStrategy::realize_figure(
    const FigureTemplate& ft, StrategyContext& ctx) {
  uint32_t seed = ft.seed ? ft.seed : ::mforce::rng::next();
  Randomizer rng(seed);
  float totalBeats = (ft.totalBeats > 0) ? ft.totalBeats : 4.0f;
  float pulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;

  // Resolve rhythm (from motif or generated)
  PulseSequence rhythm = detail::resolve_rhythm(ft, ctx, seed, totalBeats, pulse);
  int noteCount = rhythm.count();
  if (noteCount == 0) return MelodicFigure{};

  // Resolve contour (from motif or generated)
  StepSequence contour = detail::resolve_contour(ft, ctx, seed, noteCount, ft.direction);

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

inline MelodicFigure ShapeSteppingStrategy::realize_figure(
    const FigureTemplate& ft, StrategyContext& ctx) {
  uint32_t seed = ft.seed ? ft.seed : ::mforce::rng::next();
  float totalBeats = (ft.totalBeats > 0) ? ft.totalBeats : 4.0f;
  float pulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;

  PulseSequence rhythm = detail::resolve_rhythm(ft, ctx, seed, totalBeats, pulse);
  int noteCount = rhythm.count();
  if (noteCount == 0) return MelodicFigure{};

  StepSequence contour = detail::resolve_contour(ft, ctx, seed, noteCount, ft.direction);

  if (ft.targetNet != 0 && contour.count() > 1) {
    int diff = ft.targetNet - contour.net();
    contour.steps.back() += diff;
  }

  while (contour.count() < rhythm.count()) contour.add(0);
  while (contour.count() > rhythm.count()) contour.steps.pop_back();

  return MelodicFigure(rhythm, contour);
}

// ============================================================================
// Out-of-line definition of DefaultFigureStrategy::realize_figure.
//
// Lives here — BELOW the Composer class — because the body needs the full
// definition of Composer to call ctx.composer->realized_motifs(). In
// default_strategies.h, Composer is only forward-declared, so the body
// can't be inline there.
// ============================================================================

inline MelodicFigure DefaultFigureStrategy::realize_figure(
    const FigureTemplate& figTmpl, StrategyContext& ctx) {
  // Use the figure's seed for reproducibility
  uint32_t figSeed = figTmpl.seed ? figTmpl.seed : ::mforce::rng::next();

  switch (figTmpl.source) {
    case FigureSource::Locked:
      if (figTmpl.lockedFigure) return *figTmpl.lockedFigure;
      return FigureBuilder(::mforce::rng::next()).single_note(1.0f);  // fallback

    case FigureSource::Literal: {
      // Convert the user-authored note list into a MelodicFigure. Each
      // LiteralNote becomes one FigureUnit whose `step` is the delta (in
      // scale degrees) from the previous note (or from ctx.cursor
      // for the first note). Duration passes through.
      MelodicFigure fig;
      if (figTmpl.literalNotes.empty()) return fig;

      // Octave-adjusted scale-degree position: preserves direction across
      // octave boundaries. C4 = 4*7+0 = 28, G3 = 3*7+4 = 25, so C4->G3
      // yields step = -3 (down a fourth), which the conductor walks
      // correctly. Without the octave multiplier, modular-only degree math
      // would yield step = +4 and land at G4 instead of G3.
      auto absoluteDeg = [&](const Pitch& p) {
        int d = DefaultPhraseStrategy::degree_in_scale(p, ctx.scale);
        return p.octave * ctx.scale.length() + d;
      };

      int prevDeg = absoluteDeg(ctx.cursor);
      for (auto& ln : figTmpl.literalNotes) {
        FigureUnit u;
        u.duration = ln.duration;
        if (ln.rest) {
          u.rest = true;
          u.step = 0;
          // prevDeg unchanged — rests don't advance the cursor
        } else {
          if (!ln.pitch) continue;  // defensive; malformed note
          int d = absoluteDeg(*ln.pitch);
          u.step = d - prevDeg;
          prevDeg = d;
        }
        fig.units.push_back(u);
      }
      return fig;
    }

    case FigureSource::Reference: {
      auto it = ctx.composer->realized_motifs().find(figTmpl.motifName);
      if (it != ctx.composer->realized_motifs().end()) return it->second;
      // Motif not found — generate something
      return generate_figure(figTmpl, figSeed);
    }

    case FigureSource::Transform: {
      auto it = ctx.composer->realized_motifs().find(figTmpl.motifName);
      MelodicFigure base = (it != ctx.composer->realized_motifs().end())
        ? it->second
        : generate_figure(figTmpl, figSeed);
      return apply_transform(base, figTmpl.transform, figTmpl.transformParam, figSeed);
    }

    case FigureSource::Generate:
    default: {
      MelodicFigure fig;
      if (figTmpl.shape != FigureShape::Free) {
        const char* shapeName = nullptr;
        switch (figTmpl.shape) {
          case FigureShape::ScalarRun:         shapeName = "shape_scalar_run"; break;
          case FigureShape::RepeatedNote:      shapeName = "shape_repeated_note"; break;
          case FigureShape::HeldNote:          shapeName = "shape_held_note"; break;
          case FigureShape::CadentialApproach: shapeName = "shape_cadential_approach"; break;
          case FigureShape::TriadicOutline:    shapeName = "shape_triadic_outline"; break;
          case FigureShape::NeighborTone:      shapeName = "shape_neighbor_tone"; break;
          case FigureShape::LeapAndFill:       shapeName = "shape_leap_and_fill"; break;
          case FigureShape::ScalarReturn:      shapeName = "shape_scalar_return"; break;
          case FigureShape::Anacrusis:         shapeName = "shape_anacrusis"; break;
          case FigureShape::Zigzag:            shapeName = "shape_zigzag"; break;
          case FigureShape::Fanfare:           shapeName = "shape_fanfare"; break;
          case FigureShape::Sigh:              shapeName = "shape_sigh"; break;
          case FigureShape::Suspension:        shapeName = "shape_suspension"; break;
          case FigureShape::Cambiata:          shapeName = "shape_cambiata"; break;
          case FigureShape::Skipping:          shapeName = "shape_skipping"; break;
          case FigureShape::Stepping:          shapeName = "shape_stepping"; break;
          case FigureShape::Free:
          default:                             shapeName = nullptr; break;
        }
        if (shapeName) {
          Strategy* s = ctx.composer->registry_get_for_phase2(shapeName);
          if (s) {
            // Stamp figSeed into a local copy so the shape strategy uses the
            // already-consumed draw rather than pulling a second one from ctx.rng.
            FigureTemplate shapeArg = figTmpl;
            shapeArg.seed = figSeed;
            fig = s->realize_figure(shapeArg, ctx);
          }
        }
      }
      if (fig.units.empty()) {
        // Either shape was Free or the registry lookup failed — fall back.
        fig = generate_figure(figTmpl, figSeed);
      }

      // Phase 2 composition quality: proportional scaling to enforce totalBeats.
      // Applies only to Generate-path figures. Locked/Reference/Transform/Literal
      // paths take durations verbatim from the user.
      // Shapes that generate exact musical rhythms (Skipping, Stepping,
      // CadentialApproach) already sum to totalBeats — skip scaling for them
      // to preserve standard note durations.
      const bool exactRhythmShape =
          (figTmpl.shape == FigureShape::Skipping ||
           figTmpl.shape == FigureShape::Stepping ||
           figTmpl.shape == FigureShape::CadentialApproach);
      if (!exactRhythmShape && figTmpl.totalBeats > 0 && !fig.units.empty()) {
        float actual = 0;
        for (auto& u : fig.units) actual += u.duration;
        if (actual > 0 && std::abs(actual - figTmpl.totalBeats) > 0.001f) {
          float scale = figTmpl.totalBeats / actual;
          for (auto& u : fig.units) u.duration *= scale;
        }
      }
      return fig;
    }
  }
}

// ============================================================================
// Out-of-line definition of DefaultPassageStrategy::realize_passage.
//
// Lives here — BELOW the Composer class — because the body calls
// ctx.composer->realize_phrase(...) and Composer is forward-declared only in
// default_strategies.h. Same pattern as DefaultFigureStrategy::realize_figure.
// ============================================================================

inline Passage DefaultPassageStrategy::realize_passage(
    const PassageTemplate& passTmpl, StrategyContext& ctx) {
  Passage passage;

  // Initial cursor: the passage template's startingPitch (required field,
  // validated by the JSON loader at parse time — guaranteed non-nullopt here).
  if (!passTmpl.startingPitch) {
    // Should not happen — loader refuses templates without startingPitch.
    // If we're here, the template was constructed in-memory without going
    // through the loader. Emit nothing and return — caller's mistake.
    return passage;
  }
  ctx.cursor = *passTmpl.startingPitch;

  for (auto& phraseTmpl : passTmpl.phrases) {
    if (phraseTmpl.locked) continue;

    // Clone the context for the phrase level. If the phrase template has
    // an explicit startingPitch, use it as an override (resets cursor).
    // Otherwise the cursor is inherited from wherever the previous phrase
    // left it.
    StrategyContext phraseCtx = ctx;
    if (phraseTmpl.startingPitch) {
      phraseCtx.cursor = *phraseTmpl.startingPitch;
    }
    // else: phraseCtx.cursor already holds the inherited value.

    Phrase phrase = ctx.composer->realize_phrase(phraseTmpl, phraseCtx);

    // Carry the cursor forward for the next phrase. Compute the phrase's
    // net scale-degree movement by summing every unit's step across every
    // figure in the phrase, then advance a local PitchReader from the
    // phrase's starting cursor by that total.
    // IMPORTANT: compute totalDelta BEFORE std::move(phrase), as the move
    // leaves phrase.figures in a valid-but-empty moved-from state.
    PitchReader reader(ctx.scale);
    reader.set_pitch(phraseCtx.cursor);
    int totalDelta = 0;
    for (auto& fig : phrase.figures) {
      for (auto& u : fig->units) {
        totalDelta += u.step;
      }
    }
    reader.step(totalDelta);
    ctx.cursor = reader.get_pitch();

    passage.add_phrase(std::move(phrase));
  }

  return passage;
}

// ============================================================================
// Out-of-line definition of DefaultPhraseStrategy::realize_phrase.
//
// Lives here — BELOW the Composer class — because the body calls
// ctx.composer->realize_figure(...) and Composer is forward-declared only in
// default_strategies.h. Same pattern as the other two out-of-line definitions.
// ============================================================================

inline Phrase DefaultPhraseStrategy::realize_phrase(
    const PhraseTemplate& phraseTmpl, StrategyContext& ctx) {
  Phrase phrase;

  // Per-phrase starting pitch: if the template has one, use it; otherwise
  // use ctx.cursor, which DefaultPassageStrategy set to the reader's
  // reset position. Preserves pre-refactor behavior at
  // classical_composer.h:185-190.
  if (phraseTmpl.startingPitch) {
    phrase.startingPitch = *phraseTmpl.startingPitch;
  } else {
    phrase.startingPitch = ctx.cursor;
  }

  // Intra-phrase running cursor. Starts at the phrase's starting pitch
  // and advances through each figure's net_step() as figures realize.
  // Each figure's figCtx.cursor is set from this running reader before
  // dispatch, so Literal (and future Outline) figures see the correct
  // cursor position at THEIR start, not the phrase's starting pitch.
  PitchReader runningReader(ctx.scale);
  runningReader.set_pitch(phrase.startingPitch);

  const int numFigs = int(phraseTmpl.figures.size());
  for (int i = 0; i < numFigs; ++i) {
    // Apply optional connector JOINING figures[i-1] → figures[i] before
    // generating this figure. Mutates the previously-added figure in
    // phrase.figures (elides trailing units and/or adjusts last unit's
    // duration). Does NOT modify the cursor for adjustment (adjustment is
    // a duration-only change). Elision does affect the cursor since it
    // removes step contributions — we rewind the runningReader accordingly.
    if (i > 0 && i < int(phraseTmpl.connectors.size())
        && phraseTmpl.connectors[i] && !phrase.figures.empty()) {
      const auto& conn = *phraseTmpl.connectors[i];
      auto& prevFig = *phrase.figures.back();
      if (conn.elideCount > 0 && !prevFig.units.empty()) {
        int elide = std::min(conn.elideCount, int(prevFig.units.size()));
        // Rewind cursor to reflect elided step contributions
        int netElidedSteps = 0;
        for (int e = 0; e < elide; ++e) {
          netElidedSteps += prevFig.units[prevFig.units.size() - 1 - e].step;
        }
        runningReader.step(-netElidedSteps);
        prevFig.units.resize(prevFig.units.size() - elide);
      }
      if (conn.adjustCount != 0 && !prevFig.units.empty()) {
        float newDur = prevFig.units.back().duration + conn.adjustCount;
        if (newDur < 0.0f) newDur = 0.0f;  // clamp rather than error
        prevFig.units.back().duration = newDur;
      }
    }

    // MelodicFunction-driven shape selection — same logic as
    // classical_composer.h:197-202.
    FigureTemplate figTmpl = phraseTmpl.figures[i];
    if (phraseTmpl.function != MelodicFunction::Free
        && figTmpl.source == FigureSource::Generate
        && figTmpl.shape == FigureShape::Free) {
      figTmpl.shape = DefaultFigureStrategy::choose_shape(
          phraseTmpl.function, i, numFigs, ::mforce::rng::next());
    }

    // Dispatch to the figure level through the Composer. The figure context
    // clones the current (phrase) context; per-figure scale and startingPitch
    // don't need to differ in this pre-refactor path.
    StrategyContext figCtx = ctx;
    figCtx.cursor = runningReader.get_pitch();
    MelodicFigure fig = ctx.composer->realize_figure(figTmpl, figCtx);

    // Advance running cursor by the figure's net scale-degree movement.
    // Rest units contribute step=0 so they don't advance the cursor.
    runningReader.step(fig.net_step());

    // Every figure joins via its first unit's step, which bridges from
    // wherever the previous figure left the cursor to this figure's
    // effective starting pitch.
    phrase.add_melodic_figure(std::move(fig));
  }

  // Cadence adjustment — skip for Literal and Locked source on the last
  // figure template. When the user provides exact notes (Literal) or an
  // explicit locked figure, re-pitching the last unit would clobber
  // the authored pitch. Reference and Transform continue to allow
  // cadence adjustment — motif intent doesn't dictate the closing pitch.
  if (phraseTmpl.cadenceType > 0 && phraseTmpl.cadenceTarget >= 0
      && !phrase.figures.empty() && !phraseTmpl.figures.empty()) {
    const FigureSource lastSource = phraseTmpl.figures.back().source;
    if (lastSource != FigureSource::Literal && lastSource != FigureSource::Locked) {
      apply_cadence(phrase, phraseTmpl, ctx.scale);
    }
  }

  return phrase;
}

// ---------------------------------------------------------------------------
// Out-of-line definition of AlternatingFigureStrategy::realize_passage.
//
// Placed here (after Composer is fully defined) to break the circular
// dependency: AlternatingFigureStrategy calls ctx.composer->realize_figure,
// but Composer is only forward-declared in strategy.h.
// Same pattern as DefaultPassageStrategy/DefaultPhraseStrategy/DefaultFigureStrategy.
// ---------------------------------------------------------------------------
inline Passage AlternatingFigureStrategy::realize_passage(
    const PassageTemplate& pt, StrategyContext& ctx) {

  if (!ctx.chordProgression || ctx.chordProgression->count() == 0) {
    throw std::runtime_error("AFS: no chord progression in context");
  }
  if (pt.phrases.empty() || pt.phrases[0].figures.size() < 2) {
    throw std::runtime_error(
        "AFS: need 1 phrase with at least 2 figure templates (A and B)");
  }

  const auto& chordProg = *ctx.chordProgression;
  const auto& figTemplateA = pt.phrases[0].figures[0];  // chord-tone (even bars)
  const auto& figTemplateB = pt.phrases[0].figures[1];  // scalar (odd bars)

  Phrase phrase;
  phrase.startingPitch = pt.phrases[0].startingPitch.value_or(ctx.cursor);

  // PitchReader tracks the cursor across figures using scale-aware steps,
  // matching the approach used by DefaultPhraseStrategy.
  PitchReader runningReader(ctx.scale);
  runningReader.set_pitch(phrase.startingPitch);

  for (int ci = 0; ci < chordProg.count(); ++ci) {
    bool isA = (ci % 2 == 0);
    const auto& ft = isA ? figTemplateA : figTemplateB;

    // Copy template and override beat duration from progression
    FigureTemplate adjusted = ft;
    adjusted.totalBeats = chordProg.pulses.get(ci);

    // For B figures (scalar): alternate cadence types.
    // First B (ci=1) = half cadence (→V), second B (ci=3) = full cadence (→I), etc.
    if (!isA && adjusted.figureCadenceType > 0) {
      int bIndex = ci / 2;  // 0-based index among B figures
      adjusted.figureCadenceType = (bIndex % 2 == 0) ? 1 : 2;  // half, full, half, full...
    }

    // Dispatch through Composer for normal strategy/seed/motif resolution
    StrategyContext figCtx = ctx;
    figCtx.cursor = runningReader.get_pitch();
    MelodicFigure rawFig = ctx.composer->realize_figure(adjusted, figCtx);

    if (isA) {
      // For ChordFigures, simulate chord-tone stepping to track the real cursor.
      // The Conductor interprets these steps as chord tones, not scale degrees,
      // so we must do the same here to keep the cursor accurate.
      float curNN = runningReader.get_note_number();
      auto resolved = chordProg.chords.get(ci).resolve(ctx.scale, runningReader.get_octave());
      std::vector<float> tones;
      for (int os = -2; os <= 2; ++os)
        for (const auto& p : resolved.pitches)
          tones.push_back(p.note_number() + 12.0f * os);
      std::sort(tones.begin(), tones.end());
      int closest = 0;
      float minDist = 999.0f;
      for (int ti = 0; ti < int(tones.size()); ++ti) {
        float d = std::abs(tones[ti] - curNN);
        if (d < minDist) { minDist = d; closest = ti; }
      }
      int target = std::max(0, std::min(closest + rawFig.net_step(), int(tones.size()) - 1));
      runningReader.set_pitch(Pitch::from_note_number(tones[target]));

      // Wrap as ChordFigure
      auto cf = std::make_unique<ChordFigure>();
      cf->units = rawFig.units;
      phrase.add_figure(std::move(cf));
    } else {
      // For MelodicFigures, advance by scale degrees as normal
      runningReader.step(rawFig.net_step());
      phrase.add_melodic_figure(std::move(rawFig));
    }
  }

  // Update the passage-level cursor so the caller knows where we ended up
  ctx.cursor = runningReader.get_pitch();

  Passage passage;
  passage.add_phrase(std::move(phrase));
  return passage;
}

} // namespace mforce
