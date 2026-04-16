#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/strategy_registry.h"
#include "mforce/music/templates.h"
#include "mforce/music/structure.h"
#include "mforce/music/figures.h"
#include "mforce/music/piece_utils.h"
#include "mforce/music/default_strategies.h"
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

  PassageTemplate plan_passage(Locus locus, PassageTemplate seed) override;
  Passage         compose_passage(Locus locus, const PassageTemplate& pt) override;
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

  const Scale& scale = locus.piece->sections[locus.sectionIdx].scale;
  PitchReader runningReader(scale);
  runningReader.set_pitch(*pt.startingPitch);

  for (int i = 0; i < (int)pt.phrases.size(); ++i) {
    const PhraseTemplate& phraseTmpl = pt.phrases[i];
    if (phraseTmpl.locked) continue;

    // Compute this phrase's startingPitch from the local running cursor.
    // Same pattern as DefaultPassageStrategy: the passage being built is
    // local and not yet in piece.parts[], so pitch_before(locus) can't
    // see prior phrases. We track the cursor locally instead.
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

    // Advance running cursor through all realized figures in this phrase.
    for (const auto& fig : phrase.figures) {
      for (const auto& u : fig->units) {
        runningReader.step(u.step);
      }
    }

    passage.add_phrase(std::move(phrase));
  }
  return passage;
}

} // namespace mforce
