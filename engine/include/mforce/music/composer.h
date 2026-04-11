#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/strategy_registry.h"
#include "mforce/music/default_strategies.h"
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"
#include "mforce/music/pitch_reader.h"
#include "mforce/core/randomizer.h"
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
    Strategy* s = registry_.get("default_phrase");
    return s->realize_phrase(phraseTmpl, ctx);
  }

  Passage realize_passage(const PassageTemplate& passTmpl,
                          StrategyContext& ctx) {
    Strategy* s = registry_.get("default_passage");
    return s->realize_passage(passTmpl, ctx);
  }

  // --- Accessor for DefaultFigureStrategy::realize_figure ---
  const std::unordered_map<std::string, MelodicFigure>& realized_seeds() const {
    return realizedSeeds_;
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
  std::unordered_map<std::string, MelodicFigure> realizedSeeds_;

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

    // Realize seeds
    realize_seeds_(piece, tmpl);
  }

  // ---- Pre-refactor port: ClassicalComposer::realize_seeds ---------------
  void realize_seeds_(const Piece& /*piece*/, const PieceTemplate& tmpl) {
    realizedSeeds_.clear();

    // Pick a shared pulse for all generated seeds (phrase-level coherence).
    // Use piece-level default if specified, otherwise randomize once.
    float sharedPulse = tmpl.defaultPulse;
    if (sharedPulse <= 0) {
      Randomizer pulseRng(rng_.rng());
      static const float pulses[] = {0.5f, 0.5f, 1.0f, 1.0f, 1.0f, 1.5f, 2.0f};
      sharedPulse = pulses[pulseRng.int_range(0, 6)];
    }

    for (auto& seed : tmpl.seeds) {
      if (seed.userProvided || !seed.figure.units.empty()) {
        realizedSeeds_[seed.name] = seed.figure;
      } else {
        uint32_t s = seed.generationSeed ? seed.generationSeed : rng_.rng();
        FigureTemplate ft = seed.constraints.value_or(FigureTemplate{});
        // Inherit shared pulse if the seed doesn't specify its own
        if (ft.defaultPulse <= 0) ft.defaultPulse = sharedPulse;
        DefaultFigureStrategy figStrat;
        realizedSeeds_[seed.name] = figStrat.generate_figure(ft, s);
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
// definition of Composer to call ctx.composer->realized_seeds(). In
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
      // scale degrees) from the previous note (or from ctx.startingPitch
      // for the first note). Duration passes through.
      MelodicFigure fig;
      if (figTmpl.literalNotes.empty()) return fig;

      int prevDeg = DefaultPhraseStrategy::degree_in_scale(ctx.startingPitch, ctx.scale);
      for (auto& ln : figTmpl.literalNotes) {
        if (!ln.pitch) continue;  // skip notes with no pitch
        int d = DefaultPhraseStrategy::degree_in_scale(*ln.pitch, ctx.scale);
        FigureUnit u;
        u.step = d - prevDeg;
        u.duration = ln.duration;
        fig.units.push_back(u);
        prevDeg = d;
      }
      return fig;
    }

    case FigureSource::Reference: {
      auto it = ctx.composer->realized_seeds().find(figTmpl.seedName);
      if (it != ctx.composer->realized_seeds().end()) return it->second;
      // Seed not found — generate something
      return generate_figure(figTmpl, figSeed);
    }

    case FigureSource::Transform: {
      auto it = ctx.composer->realized_seeds().find(figTmpl.seedName);
      MelodicFigure base = (it != ctx.composer->realized_seeds().end())
        ? it->second
        : generate_figure(figTmpl, figSeed);
      return apply_transform(base, figTmpl.transform, figTmpl.transformParam, figSeed);
    }

    case FigureSource::Generate:
    default:
      if (figTmpl.shape != FigureShape::Free)
        return generate_shaped_figure(figTmpl, figSeed);
      return generate_figure(figTmpl, figSeed);
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
  PitchReader reader(ctx.scale);
  reader.set_pitch(5, 0);

  for (auto& phraseTmpl : passTmpl.phrases) {
    if (phraseTmpl.locked) continue;

    // Clone the context for the phrase level. We carry forward scale,
    // piece, template_, composer, rng, params; we set startingPitch to
    // the reader's reset position so DefaultPhraseStrategy can use it as
    // the fallback when the phrase template has no startingPitch of its
    // own. This preserves pre-refactor behavior where the original
    // realize_phrase did `reader.set_pitch(5, 0); reader.get_pitch();`
    // once at the start of each phrase.
    StrategyContext phraseCtx = ctx;
    reader.set_pitch(5, 0);
    phraseCtx.startingPitch = reader.get_pitch();

    Phrase phrase = ctx.composer->realize_phrase(phraseTmpl, phraseCtx);
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
  // use ctx.startingPitch, which DefaultPassageStrategy set to the reader's
  // reset position. Preserves pre-refactor behavior at
  // classical_composer.h:185-190.
  if (phraseTmpl.startingPitch) {
    phrase.startingPitch = *phraseTmpl.startingPitch;
  } else {
    phrase.startingPitch = ctx.startingPitch;
  }

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
    MelodicFigure fig = ctx.composer->realize_figure(figTmpl, figCtx);

    if (i == 0) {
      phrase.add_figure(std::move(fig));
    } else {
      // Connector path — UNCHANGED from classical_composer.h:207-213.
      // Default step(-1) when the template doesn't specify a connector.
      FigureConnector conn = FigureConnector::step(-1);
      if (i - 1 < (int)phraseTmpl.connectors.size()) {
        conn = phraseTmpl.connectors[i - 1];
      }
      phrase.add_figure(std::move(fig), conn);
    }
  }

  // Cadence adjustment — unchanged from classical_composer.h:217-222.
  if (phraseTmpl.cadenceType > 0 && phraseTmpl.cadenceTarget >= 0
      && !phrase.figures.empty()) {
    apply_cadence(phrase, phraseTmpl, ctx.scale);
  }

  return phrase;
}

} // namespace mforce
