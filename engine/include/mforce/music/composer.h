#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/strategy_registry.h"
#include "mforce/music/default_strategies.h"
#include "mforce/music/shape_strategies.h"
#include "mforce/music/phrase_strategies.h"
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"
#include "mforce/music/pitch_reader.h"
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

    // Phrase strategies (Phase 3)
    registry_.register_strategy(std::make_unique<PeriodPhraseStrategy>());
    registry_.register_strategy(std::make_unique<SentencePhraseStrategy>());
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
    Strategy* s = registry_.get("default_passage");
    return s->realize_passage(passTmpl, ctx);
  }

  // --- Accessor for DefaultFigureStrategy::realize_figure ---
  const std::unordered_map<std::string, MelodicFigure>& realized_motifs() const {
    return realizedMotifs_;
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

    // Pick a shared pulse for all generated motifs (phrase-level coherence).
    // Use piece-level default if specified, otherwise randomize once.
    float sharedPulse = tmpl.defaultPulse;
    if (sharedPulse <= 0) {
      Randomizer pulseRng(rng_.rng());
      static const float pulses[] = {0.5f, 0.5f, 1.0f, 1.0f, 1.0f, 1.5f, 2.0f};
      sharedPulse = pulses[pulseRng.int_range(0, 6)];
    }

    for (auto& motif : tmpl.motifs) {
      if (motif.userProvided || !motif.figure.units.empty()) {
        realizedMotifs_[motif.name] = motif.figure;
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

    Passage passage;
    if (passIt != partTmpl.passages.end()) {
      // Template-driven: dispatch through the registry.
      StrategyContext ctx;
      ctx.scale = scale;
      ctx.piece = &piece;
      ctx.template_ = &tmpl;
      ctx.composer = this;
      ctx.rng = &rng_;
      passage = realize_passage(passIt->second, ctx);
    } else {
      // No template for this section — fallback.
      passage = generate_default_passage_(piece, tmpl, scale);
    }

    part->passages[sectionName] = std::move(passage);
  }
};

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
  uint32_t figSeed = figTmpl.seed ? figTmpl.seed : ctx.rng->rng();

  switch (figTmpl.source) {
    case FigureSource::Locked:
      if (figTmpl.lockedFigure) return *figTmpl.lockedFigure;
      return FigureBuilder(ctx.rng->rng()).single_note(1.0f);  // fallback

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
      if (figTmpl.totalBeats > 0 && !fig.units.empty()) {
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
      for (auto& u : fig.units) {
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
    // MelodicFunction-driven shape selection — same logic as
    // classical_composer.h:197-202.
    FigureTemplate figTmpl = phraseTmpl.figures[i];
    if (phraseTmpl.function != MelodicFunction::Free
        && figTmpl.source == FigureSource::Generate
        && figTmpl.shape == FigureShape::Free) {
      figTmpl.shape = DefaultFigureStrategy::choose_shape(
          phraseTmpl.function, i, numFigs, ctx.rng->rng());
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

    // No connectors. Every figure joins via its first unit's step, which
    // bridges from wherever the previous figure left the cursor to this
    // figure's effective starting pitch.
    phrase.add_figure(std::move(fig));
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

} // namespace mforce
