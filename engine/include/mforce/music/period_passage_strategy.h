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
      // explicit consequent figures (if any) override.
      PhraseTemplate ante = p.antecedent;
      PhraseTemplate consq = p.antecedent;
      consq.cadenceType   = p.consequent.cadenceType;
      consq.cadenceTarget = p.consequent.cadenceTarget;
      if (!p.consequent.figures.empty()) {
        consq.figures = p.consequent.figures;
        consq.connectors = p.consequent.connectors;
      }
      seed.phrases.push_back(std::move(ante));
      seed.phrases.push_back(std::move(consq));
    }
    else if (p.variant == PeriodVariant::Modified) {
      // Consequent figure references derive from antecedent's via the
      // period's consequentTransform. For each Reference-source figure
      // in the consequent, swap motifName for an auto-derived motif
      // synthesized via pieceTemplate->add_derived_motif. For non-
      // Reference figures (Literal, Locked, Generate), pass through.
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
      // If the authored consequent has explicit figures, those override
      // (even in Modified — user's direct expression wins).
      if (!p.consequent.figures.empty()) {
        consq.figures = p.consequent.figures;
        consq.connectors = p.consequent.connectors;
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

  // --- Phase 1: Compose melody ---
  for (int i = 0; i < (int)pt.phrases.size(); ++i) {
    const PhraseTemplate& phraseTmpl = pt.phrases[i];
    if (phraseTmpl.locked) continue;

    PhraseTemplate localTmpl = phraseTmpl;
    if (!localTmpl.startingPitch) {
      localTmpl.startingPitch = runningReader.get_pitch();
    } else {
      runningReader.set_pitch(*localTmpl.startingPitch);
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
        auto prog = ChordWalker::walk(style, wc,
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
        auto prog = ChordWalker::walk(style, wc,
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
