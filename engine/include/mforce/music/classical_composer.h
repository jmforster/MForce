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

    // Pick a shared pulse for all generated seeds (phrase-level coherence).
    // Use piece-level default if specified, otherwise randomize once.
    float sharedPulse = tmpl.defaultPulse;
    if (sharedPulse <= 0) {
      Randomizer pulseRng(rng.rng());
      static const float pulses[] = {0.5f, 0.5f, 1.0f, 1.0f, 1.0f, 1.5f, 2.0f};
      sharedPulse = pulses[pulseRng.int_range(0, 6)];
    }

    for (auto& seed : tmpl.seeds) {
      if (seed.userProvided || !seed.figure.units.empty()) {
        realizedSeeds_[seed.name] = seed.figure;
      } else {
        uint32_t s = seed.generationSeed ? seed.generationSeed : rng.rng();
        FigureTemplate ft = seed.constraints.value_or(FigureTemplate{});
        // Inherit shared pulse if the seed doesn't specify its own
        if (ft.defaultPulse <= 0) ft.defaultPulse = sharedPulse;
        realizedSeeds_[seed.name] = generate_figure(ft, s);
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
  // Pitch-to-scale-degree helper
  // =========================================================================

  // Returns the 0-based scale degree of a pitch within the given scale,
  // or -1 if the pitch doesn't fall on a scale tone.
  static int degree_in_scale(const Pitch& pitch, const Scale& scale) {
    float rel = std::fmod(float(pitch.pitchDef->offset - scale.offset()) + 12.0f, 12.0f);
    float accum = 0;
    for (int d = 0; d < scale.length(); ++d) {
      if (std::abs(accum - rel) < 0.5f) return d;
      accum += scale.ascending_step(d);
    }
    return 0; // fallback to root
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

    int numFigs = int(phraseTmpl.figures.size());
    for (int i = 0; i < numFigs; ++i) {
      // If phrase has a MelodicFunction and this figure's shape is Free,
      // auto-select a shape based on function and position
      FigureTemplate figTmpl = phraseTmpl.figures[i];
      if (phraseTmpl.function != MelodicFunction::Free
          && figTmpl.source == FigureSource::Generate
          && figTmpl.shape == FigureShape::Free) {
        figTmpl.shape = choose_shape(phraseTmpl.function, i, numFigs,
                                      rng.rng());
      }
      MelodicFigure fig = realize_figure(figTmpl, pieceTmpl, scale);

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

    // Apply cadence: adjust last note so phrase lands on cadenceTarget
    if (phraseTmpl.cadenceType > 0 && phraseTmpl.cadenceTarget >= 0
        && !phrase.figures.empty()) {
      apply_cadence(phrase, phraseTmpl, scale);
    }

    return phrase;
  }

  // =========================================================================
  // Cadence resolution — adjust the last note to land on the target degree
  // =========================================================================

  void apply_cadence(Phrase& phrase, const PhraseTemplate& tmpl, const Scale& scale) {
    int startDeg = degree_in_scale(phrase.startingPitch, scale);
    int len = scale.length();

    // Compute net step movement across the whole phrase
    int netSteps = 0;
    for (int f = 0; f < phrase.figure_count(); ++f) {
      netSteps += phrase.figures[f].net_step();
      if (f > 0 && f - 1 < int(phrase.connectors.size())) {
        const auto& conn = phrase.connectors[f - 1];
        if (conn.type == ConnectorType::Step) netSteps += conn.stepValue;
      }
    }

    int landingDeg = ((startDeg + netSteps) % len + len) % len;
    int target = tmpl.cadenceTarget % len;

    if (landingDeg == target) return; // already correct

    // Shortest adjustment in scale degrees
    int diff = target - landingDeg;
    if (diff > len / 2) diff -= len;
    if (diff < -len / 2) diff += len;

    // Adjust the last note of the last figure
    auto& lastFig = phrase.figures.back();
    if (!lastFig.units.empty()) {
      lastFig.units.back().step += diff;
    }
  }

  // =========================================================================
  // Cadence rhythm — lengthen final notes to create a "landing" feel
  // =========================================================================

  void apply_cadence_rhythm(MelodicFigure& fig, int cadenceType) {
    int n = fig.note_count();
    if (n < 2) return;

    // Don't reshape user-provided figures (they have intentional rhythm)
    // We only reshape generated figures. Since we can't tell here, we use a
    // heuristic: if all notes have the same duration, it's likely generated.
    float firstDur = fig.units[0].duration;
    bool uniform = true;
    for (auto& u : fig.units) {
      if (std::abs(u.duration - firstDur) > 0.01f) { uniform = false; break; }
    }
    if (!uniform) return;  // already has rhythm variation — don't touch

    // Compute total beats to preserve
    float totalBeats = 0;
    for (auto& u : fig.units) totalBeats += u.duration;

    if (cadenceType >= 2 && n >= 3) {
      // Full cadence: penultimate note shorter, final note longer
      // Pattern: ...normal | short | long
      // E.g., 4 quarter notes (4 beats) → q q | e. | h  (1 + 1 + 0.5 + 1.5 = 4... no)
      // Better: redistribute last 2 notes as dotted-quarter + eighth + half
      // Actually simplest: shrink second-to-last, double the last
      float lastTwo = fig.units[n-2].duration + fig.units[n-1].duration;
      fig.units[n-2].duration = lastTwo * 0.25f;  // short pickup
      fig.units[n-1].duration = lastTwo * 0.75f;  // long resolution
    } else if (cadenceType >= 1 && n >= 2) {
      // Half cadence: just lengthen the final note
      float lastTwo = fig.units[n-2].duration + fig.units[n-1].duration;
      fig.units[n-2].duration = lastTwo * 0.4f;
      fig.units[n-1].duration = lastTwo * 0.6f;
    } else {
      // Phrase ending without explicit cadence: mild lengthening
      fig.units[n-1].duration *= 1.5f;
      // Steal time from earlier notes to keep total
      float excess = fig.units[n-1].duration - firstDur * 1.5f;
      // Actually just let it be slightly longer — total beats is approximate
    }
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
        if (figTmpl.shape != FigureShape::Free)
          return generate_shaped_figure(figTmpl, figSeed);
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
  // Generate a figure from a named shape
  // =========================================================================

  MelodicFigure generate_shaped_figure(const FigureTemplate& ft, uint32_t seed) {
    FigureBuilder fb(seed);
    fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
    int dir = ft.shapeDirection;
    int p1 = ft.shapeParam;
    int p2 = ft.shapeParam2;
    int count = (ft.maxNotes > ft.minNotes)
        ? Randomizer(seed + 99).int_range(ft.minNotes, ft.maxNotes)
        : (ft.minNotes > 0 ? ft.minNotes : 4);

    switch (ft.shape) {
      case FigureShape::ScalarRun:
        return fb.scalar_run(dir, count > 0 ? count : 4, fb.defaultPulse);

      case FigureShape::RepeatedNote:
        return fb.repeated_note(count > 0 ? count : 3, fb.defaultPulse);

      case FigureShape::HeldNote:
        return fb.held_note(ft.totalBeats > 0 ? ft.totalBeats : fb.defaultPulse * 2);

      case FigureShape::CadentialApproach:
        return fb.cadential_approach(dir < 0, p1 > 0 ? p1 : 3,
                                     fb.defaultPulse * 2, fb.defaultPulse);

      case FigureShape::TriadicOutline:
        return fb.triadic_outline(dir, p1 > 0, fb.defaultPulse);

      case FigureShape::NeighborTone:
        return fb.neighbor_tone(dir > 0, fb.defaultPulse);

      case FigureShape::LeapAndFill:
        return fb.leap_and_fill(p1 > 0 ? p1 : 4, dir > 0, p2, fb.defaultPulse);

      case FigureShape::ScalarReturn:
        return fb.scalar_return(dir, p1 > 0 ? p1 : 3, p2, fb.defaultPulse);

      case FigureShape::Anacrusis:
        return fb.anacrusis(count > 0 ? count : 2, dir,
                            fb.defaultPulse * 0.5f, fb.defaultPulse);

      case FigureShape::Zigzag:
        return fb.zigzag(dir, p1 > 0 ? p1 : 3, 2, 1, fb.defaultPulse);

      case FigureShape::Fanfare:
        return fb.fanfare({4, 3}, p1 > 0 ? p1 : 1, fb.defaultPulse);

      case FigureShape::Sigh:
        return fb.sigh(fb.defaultPulse);

      case FigureShape::Suspension:
        return fb.suspension(fb.defaultPulse * 2, fb.defaultPulse);

      case FigureShape::Cambiata:
        return fb.cambiata(dir, fb.defaultPulse);

      case FigureShape::Free:
      default:
        return generate_figure(ft, seed);
    }
  }

  // =========================================================================
  // Choose a shape based on MelodicFunction (for auto-composition)
  // =========================================================================

  FigureShape choose_shape(MelodicFunction func, int posInPhrase,
                            int totalFigures, uint32_t seed) {
    Randomizer r(seed);
    bool isLast = (posInPhrase == totalFigures - 1);
    bool isFirst = (posInPhrase == 0);

    switch (func) {
      case MelodicFunction::Statement: {
        // Opening motif: clear, memorable shapes
        static const FigureShape opts[] = {
          FigureShape::RepeatedNote, FigureShape::TriadicOutline,
          FigureShape::ScalarRun, FigureShape::NeighborTone,
          FigureShape::Fanfare
        };
        if (isLast) return FigureShape::HeldNote;
        return opts[r.int_range(0, 4)];
      }

      case MelodicFunction::Development: {
        // Extension/variation: more complex shapes
        static const FigureShape opts[] = {
          FigureShape::Zigzag, FigureShape::ScalarReturn,
          FigureShape::LeapAndFill, FigureShape::Cambiata,
          FigureShape::ScalarRun
        };
        if (isLast) return FigureShape::ScalarRun;
        return opts[r.int_range(0, 4)];
      }

      case MelodicFunction::Transition: {
        static const FigureShape opts[] = {
          FigureShape::ScalarRun, FigureShape::LeapAndFill,
          FigureShape::Zigzag, FigureShape::ScalarReturn
        };
        return opts[r.int_range(0, 3)];
      }

      case MelodicFunction::Cadential: {
        if (isFirst || !isLast)
          return FigureShape::CadentialApproach;
        return FigureShape::HeldNote;
      }

      case MelodicFunction::Free:
      default:
        return FigureShape::Free;
    }
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
