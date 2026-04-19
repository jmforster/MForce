#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/strategy_registry.h"
#include "mforce/music/templates.h"
#include "mforce/music/structure.h"
#include "mforce/music/figures.h"
#include "mforce/music/piece_utils.h"
#include "mforce/music/default_strategies.h"
#include "mforce/music/chord_walker.h"
#include <iostream>

namespace mforce {

// ---------------------------------------------------------------------------
// PeriodPassageStrategy — Option α (variant logic inlined).
//
// plan_passage: reads seed.periods[], resolves each period's variant into
// concrete PhraseTemplates for antecedent + consequent, flattens into
// seed.phrases[] in order. May mutate pieceTemplate's motif pool via
// add_derived_motif for Modified-variant consequents (Task 10 completes
// Modified + Contrasting; this task covers Parallel only).
//
// compose_passage: dispatches each flattened phrase via the registered
// PhraseStrategy (default_phrase fallback), concatenates into a Passage.
//
// Registered under name "period_passage".
// ---------------------------------------------------------------------------
class PeriodPassageStrategy : public PassageStrategy {
public:
  std::string name() const override { return "period_passage"; }
  StrategyScope scope() const override { return StrategyScope::MelodyAndHarmony; }

  PassageTemplate plan_passage(Locus locus, PassageTemplate seed) override;
  Passage         compose_passage(Locus locus, const PassageTemplate& pt) override;

private:
  // Compute the beat offset (relative to phrase start) where the cadence
  // figure begins. Walks figures, summing durations from realized motifs,
  // stopping at the last non-Connective/non-PostCadential figure.
  static float compute_cadence_beat(const PhraseTemplate& phrase,
                                     const PieceTemplate& tmpl) {
    // Find the index of the cadence figure (last non-Connective/PostCadential)
    int lastIdx = (int)phrase.figures.size() - 1;
    while (lastIdx >= 0) {
      const auto& ft = phrase.figures[lastIdx];
      if (ft.role && (*ft.role == MotifRole::Connective || *ft.role == MotifRole::PostCadential)) {
        --lastIdx;
        continue;
      }
      break;
    }
    if (lastIdx < 0) return 0.0f;

    // Sum durations of all figures BEFORE the cadence figure
    float beat = 0.0f;
    for (int i = 0; i < lastIdx; ++i) {
      const auto& ft = phrase.figures[i];
      if (ft.source == FigureSource::Reference && !ft.motifName.empty()) {
        auto it = tmpl.realizedMotifs.find(ft.motifName);
        if (it != tmpl.realizedMotifs.end()) {
          beat += it->second.total_duration();
          continue;
        }
      }
      // Fallback: use totalBeats from template if available
      if (ft.totalBeats > 0) beat += ft.totalBeats;
      else beat += 4.0f; // last resort
    }
    return beat;
  }

  // Build a melody profile from composed phrases for a given beat range.
  // Walks the passage's phrases, tracking pitch via scale degree.
  static std::vector<MelodySpan> build_melody_profile(
      const Passage& passage, const Scale& scale,
      float startBeat, float endBeat) {
    std::vector<MelodySpan> profile;
    float beat = 0.0f;

    for (int pi = 0; pi < passage.phrase_count(); ++pi) {
      const Phrase& phrase = passage.phrases[pi];
      // Compute starting degree from phrase's startingPitch
      int degree = DefaultPhraseStrategy::degree_in_scale(
          phrase.startingPitch, scale);

      for (const auto& fig : phrase.figures) {
        for (const auto& u : fig->units) {
          // Apply step FIRST — the unit sounds at degree+step, not degree
          int len = scale.length();
          degree = ((degree + u.step) % len + len) % len;
          if (beat + u.duration > startBeat && beat < endBeat) {
            float spanStart = std::max(beat, startBeat) - startBeat;
            float spanEnd = std::min(beat + u.duration, endBeat) - startBeat;
            profile.push_back({spanStart, spanEnd - spanStart, degree, u.rest});
          }
          beat += u.duration;
        }
      }
    }
    return profile;
  }

  static ScaleChord cadence_chord(int cadenceType, int /*targetDegree*/) {
    if (cadenceType == 1) {
      return ScaleChord{4, 0, &ChordDef::get("Major")};   // V (HC)
    } else {
      return ScaleChord{0, 0, &ChordDef::get("Major")};   // I (PAC/IAC)
    }
  }
};

// --- Out-of-line definitions -------------------------------------------------

inline PassageTemplate PeriodPassageStrategy::plan_passage(
    Locus locus, PassageTemplate seed) {

  // If the seed has no periods[], there's nothing to plan — just return
  // the seed unchanged. Author probably intends to drive via seed.phrases[]
  // with a different strategy; but if they pointed at period_passage
  // without periods, we pass through rather than throwing.
  if (seed.periods.empty()) return seed;

  // Reset phrases[]; we'll populate from periods[]. If the user supplied
  // pre-resolved phrases alongside periods, we overwrite — periods win.
  seed.phrases.clear();

  // Auto-generate motifs for Generate-source figures in antecedents.
  // This converts them to Reference-source so the Parallel/Modified
  // variant logic can reuse/transform them in the consequent.
  for (int pi = 0; pi < (int)seed.periods.size(); ++pi) {
    PeriodSpec& p = seed.periods[pi];
    auto autoGenFigures = [&](PhraseTemplate& phrase, const std::string& suffix) {
      int numFigs = (int)phrase.figures.size();
      for (int fi = 0; fi < numFigs; ++fi) {
        auto& ft = phrase.figures[fi];
        if (ft.source != FigureSource::Generate) continue;

        // Per-figure function takes precedence; fall back to phrase function.
        MelodicFunction func = (ft.function != MelodicFunction::Free)
                              ? ft.function
                              : phrase.function;

        uint32_t figSeed = ::mforce::rng::next();
        FigureTemplate genTmpl = ft;
        genTmpl.seed = figSeed;

        if (genTmpl.shape == FigureShape::Free && func != MelodicFunction::Free) {
          genTmpl.shape = DefaultFigureStrategy::choose_shape(func, fi, numFigs, figSeed);
        }

        // Dispatch through the figure strategy so shapes actually run through
        // the shape registry (compose_figure handles Generate-source by
        // resolving ft.shape → ShapeXxxStrategy). generate_figure alone
        // ignores shape and produces random-step output only.
        DefaultFigureStrategy figStrat;
        MelodicFigure fig = figStrat.compose_figure(locus, genTmpl);

        std::string motifName = "auto_p" + std::to_string(pi) + suffix + std::to_string(fi);
        Motif m;
        m.name = motifName;
        m.userProvided = false;
        m.content = fig;
        locus.pieceTemplate->add_motif(std::move(m));

        ft.source = FigureSource::Reference;
        ft.motifName = motifName;
      }
    };

    autoGenFigures(p.antecedent, "_f");
    // For Parallel/Modified, the consequent inherits/transforms from the
    // antecedent via variant resolution below — don't auto-gen independent
    // motifs that would break parallelism. For Contrasting, Generate figures
    // are resolved at compose time by the figure strategy.
    if (p.variant == PeriodVariant::Contrasting) {
      autoGenFigures(p.consequent, "_c");
    }
  }

  for (int pi = 0; pi < (int)seed.periods.size(); ++pi) {
    PeriodSpec& p = seed.periods[pi];

    // Optional leading connective phrase (between periods, not before period 0).
    if (pi > 0 && p.leadingConnective) {
      PhraseTemplate connPhrase;
      connPhrase.name = "connective_" + std::to_string(pi);
      FigureTemplate ft;
      ft.source = FigureSource::Reference;
      ft.motifName = *p.leadingConnective;
      connPhrase.figures.push_back(ft);
      FigureConnector fc;  // default: all zeros
      connPhrase.connectors.push_back(fc);
      seed.phrases.push_back(std::move(connPhrase));
    }

    // Resolve variant into two concrete PhraseTemplates.
    if (p.variant == PeriodVariant::Parallel) {
      // Consequent starts as a copy of antecedent; cadence fields and
      // per-figure explicit consequent overrides apply.
      PhraseTemplate ante = p.antecedent;
      PhraseTemplate consq = p.antecedent;
      consq.cadenceType   = p.consequent.cadenceType;
      consq.cadenceTarget = p.consequent.cadenceTarget;
      // Per-figure override: a Generate-source figure at index i means
      // "inherit from antecedent"; any other source (Reference/Literal/Locked)
      // is treated as a user-authored override for that slot (typical use:
      // cadence-tail override).
      for (size_t i = 0; i < p.consequent.figures.size() && i < consq.figures.size(); ++i) {
        if (p.consequent.figures[i].source != FigureSource::Generate) {
          consq.figures[i] = p.consequent.figures[i];
          if (i < p.consequent.connectors.size() && i < consq.connectors.size()) {
            consq.connectors[i] = p.consequent.connectors[i];
          }
        }
      }
      seed.phrases.push_back(std::move(ante));
      seed.phrases.push_back(std::move(consq));
    }
    else if (p.variant == PeriodVariant::Modified) {
      // Consequent figure references derive from antecedent's via the
      // period's consequentTransform. For each Reference-source figure
      // in the consequent, swap motifName for an auto-derived motif
      // synthesized via pieceTemplate->add_derived_motif.
      PhraseTemplate ante = p.antecedent;
      PhraseTemplate consq = p.antecedent;  // start from antecedent

      if (p.consequentTransform) {
        for (auto& ft : consq.figures) {
          if (ft.source == FigureSource::Reference && !ft.motifName.empty()) {
            std::string derivedName = locus.pieceTemplate->add_derived_motif(
                ft.motifName,
                *p.consequentTransform,
                p.consequentTransformParam);
            ft.motifName = derivedName;
          }
        }
      }

      consq.cadenceType   = p.consequent.cadenceType;
      consq.cadenceTarget = p.consequent.cadenceTarget;
      // Per-figure override (same rule as Parallel): Generate means inherit;
      // any other source replaces for that slot.
      for (size_t i = 0; i < p.consequent.figures.size() && i < consq.figures.size(); ++i) {
        if (p.consequent.figures[i].source != FigureSource::Generate) {
          consq.figures[i] = p.consequent.figures[i];
          if (i < p.consequent.connectors.size() && i < consq.connectors.size()) {
            consq.connectors[i] = p.consequent.connectors[i];
          }
        }
      }
      seed.phrases.push_back(std::move(ante));
      seed.phrases.push_back(std::move(consq));
    }
    else { // PeriodVariant::Contrasting
      // Consequent uses its own authored figures entirely. Antecedent and
      // consequent are independent.
      seed.phrases.push_back(p.antecedent);
      seed.phrases.push_back(p.consequent);
    }
  }

  return seed;
}

inline Passage PeriodPassageStrategy::compose_passage(
    Locus locus, const PassageTemplate& pt) {
  Passage passage;

  if (!pt.startingPitch) return passage;

  const auto& sec = locus.piece->sections[locus.sectionIdx];
  const Scale& scale = sec.scale;
  PitchReader runningReader(scale);
  runningReader.set_pitch(*pt.startingPitch);

  // Build a role map: mark which flat phrase indices are the antecedent or
  // consequent of a Parallel/Modified period so we can force the consequent
  // to start at the same pitch as its antecedent (required for audible
  // parallelism — matching step sequences with different start pitches
  // still sound like different melodies).
  enum class PhraseRole : uint8_t { Normal, Ante, Consq };
  std::vector<PhraseRole> roles(pt.phrases.size(), PhraseRole::Normal);
  {
    int flat = 0;
    for (int pi = 0; pi < (int)pt.periods.size(); ++pi) {
      const PeriodSpec& p = pt.periods[pi];
      if (pi > 0 && p.leadingConnective) ++flat;  // connective phrase
      bool paired = (p.variant == PeriodVariant::Parallel ||
                     p.variant == PeriodVariant::Modified);
      if (flat < (int)roles.size() && paired) roles[flat] = PhraseRole::Ante;
      ++flat;
      if (flat < (int)roles.size() && paired) roles[flat] = PhraseRole::Consq;
      ++flat;
    }
  }

  // --- Phase 1: Compose melody ---
  Pitch anteStartPitch;  // captured before composing an Ante, used by paired Consq
  for (int i = 0; i < (int)pt.phrases.size(); ++i) {
    const PhraseTemplate& phraseTmpl = pt.phrases[i];
    if (phraseTmpl.locked) continue;

    PhraseTemplate localTmpl = phraseTmpl;
    if (roles[i] == PhraseRole::Consq) {
      // Force consequent to start at antecedent's pitch for audible parallelism.
      localTmpl.startingPitch = anteStartPitch;
      runningReader.set_pitch(anteStartPitch);
    } else if (!localTmpl.startingPitch) {
      localTmpl.startingPitch = runningReader.get_pitch();
    } else {
      runningReader.set_pitch(*localTmpl.startingPitch);
    }
    if (roles[i] == PhraseRole::Ante) {
      anteStartPitch = *localTmpl.startingPitch;
    }

    std::string pn = localTmpl.strategy.empty()
                     ? std::string("default_phrase")
                     : localTmpl.strategy;
    PhraseStrategy* ps = StrategyRegistry::instance().resolve_phrase(pn);
    if (!ps) {
      std::cerr << "Unknown phrase strategy '" << pn
                << "', falling back to default_phrase\n";
      ps = StrategyRegistry::instance().resolve_phrase("default_phrase");
    }
    Phrase phrase = ps->compose_phrase(locus.with_phrase(i), localTmpl);

    for (const auto& fig : phrase.figures) {
      for (const auto& u : fig->units) {
        runningReader.step(u.step);
      }
    }

    passage.add_phrase(std::move(phrase));
  }

  // --- Phase 2: Generate harmony from composed melody ---
  const PieceTemplate::SectionTemplate* sd = nullptr;
  for (const auto& s : locus.pieceTemplate->sections) {
    if (s.name == sec.name) { sd = &s; break; }
  }

  if (sd && !sd->styleName.empty() && sec.harmonyTimeline.empty()
      && !pt.periods.empty()) {
    auto style = StyleTable::load_by_name(sd->styleName);
    float beatOffset = 0.0f;

    for (int pi = 0; pi < (int)pt.periods.size(); ++pi) {
      const PeriodSpec& period = pt.periods[pi];
      float barsPerPhrase = period.bars / 2.0f;
      float beatsPerBar = float(sec.meter.beats_per_bar());
      float anteBeats = barsPerPhrase * beatsPerBar;
      float consBeats = barsPerPhrase * beatsPerBar;

      // Build attack beats at half-note intervals (2 beats)
      auto make_attacks = [](float totalBeats) {
        std::vector<float> attacks;
        for (float b = 0; b < totalBeats - 0.01f; b += 2.0f)
          attacks.push_back(b);
        return attacks;
      };

      // Antecedent: I -> cadence target
      ScaleChord anteEnd{0, 0, &ChordDef::get("Major")};
      {
        WalkConstraint wc;
        wc.startChord = ScaleChord{0, 0, &ChordDef::get("Major")};
        if (period.antecedent.cadenceType > 0) {
          anteEnd = cadence_chord(period.antecedent.cadenceType,
                                  period.antecedent.cadenceTarget);
          wc.endChord = anteEnd;
          wc.cadenceBeat = compute_cadence_beat(period.antecedent, *locus.pieceTemplate);
        }
        wc.melodyProfile = build_melody_profile(
            passage, scale, beatOffset, beatOffset + anteBeats);
        wc.totalBeats = anteBeats;
        auto attacks = make_attacks(anteBeats);
        auto prog = ChordWalker::harmonize(style, wc, attacks,
            locus.pieceTemplate->masterSeed + pi * 100);
        const_cast<Section&>(sec).harmonyTimeline.set_segment(
            beatOffset, beatOffset + anteBeats, prog, "period_passage");
        beatOffset += anteBeats;
      }

      // Consequent: starts on antecedent's cadence chord
      {
        WalkConstraint wc;
        wc.startChord = anteEnd;
        if (period.consequent.cadenceType > 0) {
          wc.endChord = cadence_chord(period.consequent.cadenceType,
                                       period.consequent.cadenceTarget);
          wc.cadenceBeat = compute_cadence_beat(period.consequent, *locus.pieceTemplate);
        }
        wc.melodyProfile = build_melody_profile(
            passage, scale, beatOffset, beatOffset + consBeats);
        wc.totalBeats = consBeats;
        auto attacks = make_attacks(consBeats);
        auto prog = ChordWalker::harmonize(style, wc, attacks,
            locus.pieceTemplate->masterSeed + pi * 100 + 50);
        const_cast<Section&>(sec).harmonyTimeline.set_segment(
            beatOffset, beatOffset + consBeats, prog, "period_passage");
        beatOffset += consBeats;
      }
    }
  }

  return passage;
}

} // namespace mforce
