#pragma once
#include "mforce/music/compose.h"
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"
#include "mforce/music/figures.h"
#include "mforce/music/pitch_reader.h"
#include "mforce/core/randomizer.h"
#include <unordered_map>

namespace mforce {

// ---------------------------------------------------------------------------
// ClassicalComposer — template-driven melodic composition.
//
// Walks a PieceTemplate and realizes each FigureTemplate according to its
// source mode (Generate, Reference, Transform, Locked). When template
// details are sparse, falls back to genre-appropriate defaults.
// ---------------------------------------------------------------------------
struct ClassicalComposer : IComposer {
  StepGenerator stepGen;
  FigureBuilder builder;
  Randomizer rng;

  explicit ClassicalComposer(uint32_t seed = 0xC1A5'0001u)
    : stepGen(seed), builder(seed + 100), rng(seed + 200) {}

  std::string name() const override { return "Classical"; }

  // --- Top-level: compose everything ---
  void compose(Piece& piece, const PieceTemplate& tmpl) override {
    // Set up piece structure from template
    setup_piece(piece, tmpl);

    // Compose each part across all sections
    for (auto& partTmpl : tmpl.parts) {
      compose(piece, tmpl, partTmpl.name);
    }
  }

  // --- Compose all sections for one part ---
  void compose(Piece& piece, const PieceTemplate& tmpl,
               const std::string& partName) override {
    for (auto& sec : piece.sections) {
      compose(piece, tmpl, partName, sec.name);
    }
  }

  // --- Compose one passage (one part + one section) ---
  void compose(Piece& piece, const PieceTemplate& tmpl,
               const std::string& partName,
               const std::string& sectionName) override {

    // Find the part
    Part* part = nullptr;
    for (auto& p : piece.parts) if (p.name == partName) { part = &p; break; }
    if (!part) return;

    // Find the part template
    const PartTemplate* partTmpl = nullptr;
    for (auto& pt : tmpl.parts) if (pt.name == partName) { partTmpl = &pt; break; }
    if (!partTmpl) return;

    // Find the passage template for this section
    auto passIt = partTmpl->passages.find(sectionName);

    Scale scale = piece.key.scale;

    if (passIt != partTmpl->passages.end()) {
      // Template-driven: walk the recipe
      Passage passage = realize_passage(passIt->second, tmpl, scale);
      part->passages[sectionName] = std::move(passage);
    } else {
      // No template for this section — generate a default passage
      Passage passage = generate_default_passage(scale);
      part->passages[sectionName] = std::move(passage);
    }
  }

private:
  // Map of realized seed figures (populated during composition)
  std::unordered_map<std::string, MelodicFigure> realizedSeeds_;

  // =========================================================================
  // Setup
  // =========================================================================

  void setup_piece(Piece& piece, const PieceTemplate& tmpl) {
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
    realize_seeds(tmpl);
  }

  void realize_seeds(const PieceTemplate& tmpl) {
    realizedSeeds_.clear();
    for (auto& seed : tmpl.seeds) {
      if (seed.userProvided || !seed.figure.units.empty()) {
        // Use as-is
        realizedSeeds_[seed.name] = seed.figure;
      } else {
        // Generate a seed figure
        Randomizer seedRng(seed.generationSeed ? seed.generationSeed : rng.rng());
        StepGenerator sg(seedRng.rng());
        FigureBuilder fb(seedRng.rng());
        fb.defaultPulse = 1.0f;
        auto ss = sg.random_sequence(3);
        realizedSeeds_[seed.name] = fb.build(ss, 1.0f);
      }
    }
  }

  // =========================================================================
  // Realize a passage from its template
  // =========================================================================

  Passage realize_passage(const PassageTemplate& passTmpl, const PieceTemplate& pieceTmpl,
                          const Scale& scale) {
    Passage passage;
    PitchReader reader(scale);
    reader.set_pitch(5, 0);

    for (auto& phraseTmpl : passTmpl.phrases) {
      if (phraseTmpl.locked) {
        // Skip — would use existing composed phrase if we had one
        continue;
      }
      Phrase phrase = realize_phrase(phraseTmpl, pieceTmpl, scale, reader);
      passage.add_phrase(std::move(phrase));
    }

    return passage;
  }

  // =========================================================================
  // Realize a phrase from its template
  // =========================================================================

  Phrase realize_phrase(const PhraseTemplate& phraseTmpl, const PieceTemplate& pieceTmpl,
                        const Scale& scale, PitchReader& reader) {
    Phrase phrase;

    if (phraseTmpl.startingPitch) {
      phrase.startingPitch = *phraseTmpl.startingPitch;
    } else {
      reader.set_pitch(5, 0);
      phrase.startingPitch = reader.get_pitch();
    }

    for (int i = 0; i < (int)phraseTmpl.figures.size(); ++i) {
      MelodicFigure fig = realize_figure(phraseTmpl.figures[i], pieceTmpl, scale);

      if (i == 0) {
        phrase.add_figure(std::move(fig));
      } else {
        // Use connector from template if available, else default step(-1)
        FigureConnector conn = FigureConnector::step(-1);
        if (i - 1 < (int)phraseTmpl.connectors.size()) {
          conn = phraseTmpl.connectors[i - 1];
        }
        phrase.add_figure(std::move(fig), conn);
      }
    }

    return phrase;
  }

  // =========================================================================
  // Realize a single figure from its template
  // =========================================================================

  MelodicFigure realize_figure(const FigureTemplate& figTmpl, const PieceTemplate& pieceTmpl,
                                const Scale& scale) {
    // Use the figure's seed for reproducibility
    uint32_t figSeed = figTmpl.seed ? figTmpl.seed : rng.rng();

    switch (figTmpl.source) {
      case FigureSource::Locked:
        if (figTmpl.lockedFigure) return *figTmpl.lockedFigure;
        return builder.single_note(1.0f);  // fallback

      case FigureSource::Reference: {
        auto it = realizedSeeds_.find(figTmpl.seedName);
        if (it != realizedSeeds_.end()) return it->second;
        // Seed not found — generate something
        return generate_figure(figTmpl, figSeed);
      }

      case FigureSource::Transform: {
        auto it = realizedSeeds_.find(figTmpl.seedName);
        MelodicFigure base = (it != realizedSeeds_.end())
          ? it->second
          : generate_figure(figTmpl, figSeed);
        return apply_transform(base, figTmpl.transform, figTmpl.transformParam, figSeed);
      }

      case FigureSource::Generate:
      default:
        return generate_figure(figTmpl, figSeed);
    }
  }

  // =========================================================================
  // Generate a figure from constraints
  // =========================================================================

  MelodicFigure generate_figure(const FigureTemplate& figTmpl, uint32_t seed) {
    StepGenerator sg(seed);
    FigureBuilder fb(seed + 1);

    fb.defaultPulse = (figTmpl.defaultPulse > 0) ? figTmpl.defaultPulse : 1.0f;

    if (figTmpl.totalBeats > 0) {
      // Build from total length
      int noteCount = int(figTmpl.totalBeats / fb.defaultPulse);
      noteCount = std::clamp(noteCount, figTmpl.minNotes, figTmpl.maxNotes);

      StepSequence ss;
      if (figTmpl.targetNet != 0) {
        ss = sg.targeted_sequence(noteCount - 1, figTmpl.targetNet);
      } else if (figTmpl.preferStepwise) {
        ss = sg.no_skip_sequence(noteCount - 1);
      } else {
        float skipProb = figTmpl.preferSkips ? 0.6f : 0.3f;
        ss = sg.random_sequence(noteCount - 1, skipProb);
      }

      MelodicFigure fig = fb.build(ss, fb.defaultPulse);

      // Apply random rhythm variation
      Randomizer varyRng(seed + 2);
      if (varyRng.decide(0.4f)) {
        fig = fb.vary_rhythm(fig);
      }

      return fig;
    }

    // No total beats specified — use note count range
    Randomizer countRng(seed + 3);
    int noteCount = countRng.int_range(figTmpl.minNotes, figTmpl.maxNotes);

    StepSequence ss;
    if (figTmpl.targetNet != 0) {
      ss = sg.targeted_sequence(noteCount - 1, figTmpl.targetNet);
    } else if (figTmpl.preferStepwise) {
      ss = sg.no_skip_sequence(noteCount - 1);
    } else {
      ss = sg.random_sequence(noteCount - 1);
    }

    return fb.build(ss, fb.defaultPulse);
  }

  // =========================================================================
  // Apply a transformation to a figure
  // =========================================================================

  MelodicFigure apply_transform(const MelodicFigure& base, TransformOp op,
                                 int param, uint32_t seed) {
    FigureBuilder fb(seed);

    switch (op) {
      case TransformOp::Invert:
        return fb.invert(base);

      case TransformOp::Reverse:
        return fb.reverse(base);

      case TransformOp::Stretch:
        return fb.stretch(base, param > 0 ? float(param) : 2.0f);

      case TransformOp::Compress:
        return fb.compress(base, param > 0 ? float(param) : 2.0f);

      case TransformOp::VaryRhythm:
        return fb.vary_rhythm(base);

      case TransformOp::VarySteps: {
        MelodicFigure copy = base;
        return fb.vary_steps(copy, std::max(1, param));
      }

      case TransformOp::NewSteps: {
        // Keep rhythm, generate new steps
        StepGenerator sg(seed);
        StepSequence newSS = sg.random_sequence(base.note_count() - 1);
        return fb.build(newSS, base.units[0].duration);
      }

      case TransformOp::NewRhythm: {
        // Keep steps, generate new rhythm — rebuild with random pulses
        MelodicFigure fig = base;
        Randomizer rr(seed);
        for (auto& u : fig.units) {
          u.duration *= (rr.decide(0.5f) ? 0.5f : 1.0f) * (rr.decide(0.3f) ? 1.5f : 1.0f);
        }
        return fig;
      }

      case TransformOp::Replicate: {
        int count = (param > 0) ? param : 2;
        Randomizer rr(seed);
        int step = rr.select_int({-2, -1, 1, 2});
        return fb.replicate(base, count, step);
      }

      case TransformOp::TransformGeneral: {
        // Composer picks a random transform
        Randomizer rr(seed);
        float choice = rr.value();
        if (choice < 0.25f) return fb.invert(base);
        if (choice < 0.50f) return fb.vary_rhythm(base);
        if (choice < 0.75f) return fb.reverse(base);
        return fb.stretch(base);
      }

      case TransformOp::None:
      default:
        return base;
    }
  }

  // =========================================================================
  // Default passage (when no template specified for a section)
  // =========================================================================

  Passage generate_default_passage(const Scale& scale) {
    PitchReader reader(scale);
    reader.set_pitch(5, 0);
    Pitch startPitch = reader.get_pitch();

    // Simple binary form: antecedent + consequent
    uint32_t s1 = rng.rng(), s2 = rng.rng();

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

    PieceTemplate dummyTmpl;
    return realize_passage(passTmpl, dummyTmpl, scale);
  }
};

} // namespace mforce
