#pragma once
#include "mforce/compose/strategy.h"
#include "mforce/compose/strategy_registry.h"
#include "mforce/music/figures.h"
#include "mforce/compose/figure_transforms.h"
#include "mforce/compose/random_figure_builder.h"
#include "mforce/compose/pool_figure_builder.h"
#include "mforce/music/structure.h"
#include "mforce/compose/templates.h"
#include "mforce/music/pitch_reader.h"
#include "mforce/core/randomizer.h"
#include "mforce/compose/environment.h"
#include <algorithm>
#include <cmath>
#include <memory>

#include "mforce/compose/anchor_selector.h"
#include "mforce/compose/compose.h"
#include "mforce/compose/melody_profile.h"
#include "mforce/compose/piece_utils.h"
#include <iostream>
#include <set>
#include <unordered_map>

namespace mforce {

// ---------------------------------------------------------------------------
// DefaultPassageStrategy
//
// Walks the phrase list of a PassageTemplate and realizes each phrase via
// the Composer's phrase dispatcher. A phrase without its own startingPitch
// starts where the phrases before it in this passage ended, walked from the
// passage's start with a local PitchReader; a parallel phrase restarts at
// the passage's start.
//
// Body is DECLARED in the class and DEFINED below it.
// ---------------------------------------------------------------------------
class DefaultPassageStrategy : public PassageStrategy {
public:
  static constexpr const char* kName = "default_passage";
  using Settings = NoSettings;

  std::string name() const override { return kName; }

  Passage compose_passage(Locus locus, const PassageTemplate& passTmpl) const override;
};

// ============================================================================
// Out-of-line definition of DefaultPassageStrategy::compose_passage. It
// looks up phrase strategies in the kit, which needs ComposeKit complete.
// ============================================================================

inline Passage DefaultPassageStrategy::compose_passage(
    Locus locus, const PassageTemplate& passTmpl) const {
  (void)settings_of<Settings>(passTmpl.strategy);  // refuses settings of another type
  // The loader accepts a passage without a startingPitch. Such a passage
  // starts on the startingPitch of its first phrase that has one; when none has, the
  // passage is refused by name.
  std::optional<Pitch> passageStart = passTmpl.startingPitch;
  if (!passageStart) {
    for (const auto& ph : passTmpl.phrases) {
      if (ph.startingPitch) { passageStart = ph.startingPitch; break; }
    }
  }
  if (!passageStart) {
    throw std::runtime_error(
        "default_passage: passage '" + passTmpl.name +
        "' has no startingPitch and no phrase supplies one");
  }

  // Harmonic anchor mode: requires a genre profile.
  const bool harmonicMode = passTmpl.anchorMode == "harmonic";
  const bool wantAnchorLog =
      harmonicMode && environment_variable_is_set("MFORCE_ANCHOR_LOG");
  MelodyProfile melodyProfile;
  if (harmonicMode) {
    if (passTmpl.melodyProfile.empty())
      throw std::runtime_error(
          "anchorMode harmonic requires melodyProfile (passage '" +
          passTmpl.name + "')");
    melodyProfile = MelodyProfile::load_by_name(passTmpl.melodyProfile);
  }

  // The best-of-N search rerolls derived motifs and commits the winners, so it
  // WRITES the plan's realized motif pool (the same pool realize_motifs
  // fills at setup). Phrases composed later —
  // in this passage and any other — read the committed derivations.
  // Null (never touched) outside harmonic mode.
  using MotifPool = std::unordered_map<std::string, MelodicFigure>;
  MotifPool* const realizedPool =
      harmonicMode ? &locus.plan->realizedMotifs : nullptr;

  // Rerollable = a content-less derived figure motif without a pinned
  // generationSeed (spec §3: pinned motifs are never rerolled).
  std::unordered_map<std::string, const Motif*> motifByName;
  if (harmonicMode)
    for (const auto& m : locus.plan->motifs) motifByName[m.name] = &m;
  auto is_derived = [&](const Motif* m) {
    return m && m->is_figure() && m->figure().units.empty() && m->derivedFrom;
  };
  auto rerollable = [&](const Motif* m) {
    return is_derived(m) && m->generationSeed == 0;
  };

  struct PassageAttempt {
    Passage passage;
    double score{0};
    bool inBudget{true};
    MotifPool motifs;
    std::string log;
  };

  // One full pass over the phrases. The non-harmonic path runs this exactly
  // once with no extra draws.
  auto run_attempt = [&](PassageAttempt& A) {
    Passage& passage = A.passage;
    float phraseBeatCursor = 0.0f;          // section beat at phrase start
    std::optional<Pitch> lastChosenStart;   // harmonic mode: parallel pinning
    std::vector<TrackNote> priorTrack;      // harmonic mode: passage so far
    std::set<std::string> committed;        // harmonic mode: motifs owned so far

    for (int i = 0; i < (int)passTmpl.phrases.size(); ++i) {
      const auto& phraseTmpl = passTmpl.phrases[i];
      if (phraseTmpl.locked) continue;

      // Build a phrase-level Locus for the child. Note that piece_utils::
      // pitch_before(locus.with_phrase(i)) walks the realized phrases so far,
      // but at this point `passage` is local — it hasn't been inserted into
      // piece.parts[partIdx].passages yet, so pitch_before sees an empty
      // passage and falls back to the template's startingPitch. This is
      // correct for the first phrase. For subsequent phrases, pitch_before
      // can't see our local passage — we must pass the cursor via the phrase
      // template's startingPitch, computed from the passage so far.
      PhraseTemplate localTmpl = phraseTmpl;
      if (!localTmpl.startingPitch && localTmpl.parallel) {
        // Parallel phrase: restart where the passage started (i.e. where the
        // first phrase began) instead of continuing from the running pitch.
        // A parallel consequent so starts on the antecedent's opening pitch
        // whatever the running pitch.
        localTmpl.startingPitch = passageStart;
      }
      if (!localTmpl.startingPitch) {
        // Compute cursor from the phrases realized so far in our local passage.
        Pitch cursor = *passageStart;
        const Scale& scale = locus.piece->sections[locus.sectionIdx].scale;
        PitchReader reader(scale);
        reader.set_pitch(cursor);
        for (auto& ph : passage.phrases) {
          for (int fi = 0; fi < (int)ph.figures.size(); ++fi) {
            if (fi < (int)ph.connectors.size()) reader.step(ph.connectors[fi].leadStep);
            for (auto& u : ph.figures[fi]->units) {
              reader.step(u.step);
            }
          }
        }
        localTmpl.startingPitch = reader.get_pitch();
      }

      // Harmonic anchor mode: derive this phrase's startingPitch +
      // connectors from the chord timeline instead of the authored values.
      // The authored pitch anchors only the REGISTER; a parallel phrase
      // reuses the previous phrase's CHOSEN start (intent outranks R1).
      // Phrase level of the search: N candidates, each rerolling the derived
      // motifs this phrase references FIRST, each running the chain level;
      // top-k dice among the ranked candidates; the winner's derivations
      // are committed for every later reference.
      if (harmonicMode) {
        const Section& sec = locus.piece->sections[locus.sectionIdx];
        MotifPool& realized = *realizedPool;

        // ---- Owned motifs: first referenced here (plus derived ancestors
        //      not yet committed), rerollable. Pinned derived motifs whose
        //      parent is rerolled here are re-derived with their own seed
        //      so they stay consistent with it (not rerolled). ----
        std::set<std::string> owned;
        for (const auto& ft : localTmpl.figures) {
          if (ft.source != FigureSource::Reference) continue;
          std::string name = ft.motifName;
          while (true) {
            auto it = motifByName.find(name);
            if (it == motifByName.end() || !rerollable(it->second)) break;
            if (committed.count(name)) break;
            owned.insert(name);
            name = *it->second->derivedFrom;
          }
        }
        std::vector<const Motif*> rederive;   // dependency order
        {
          std::set<std::string> touched = owned;
          bool progress = true;
          std::set<std::string> placed;
          while (progress) {
            progress = false;
            for (const auto& m : locus.plan->motifs) {
              if (placed.count(m.name) || !is_derived(&m)) continue;
              const bool parentTouched = touched.count(*m.derivedFrom) > 0;
              const bool isOwned = owned.count(m.name) > 0;
              const bool pinnedFollower = !isOwned && parentTouched
                  && m.generationSeed != 0 && !committed.count(m.name);
              if (!isOwned && !pinnedFollower) continue;
              // Parent first when it is also re-derived here.
              if (touched.count(*m.derivedFrom) && !placed.count(*m.derivedFrom)
                  && motifByName.count(*m.derivedFrom)
                  && is_derived(motifByName.at(*m.derivedFrom)))
                continue;
              rederive.push_back(&m);
              placed.insert(m.name);
              touched.insert(m.name);
              progress = true;
            }
          }
        }
        MotifPool entry;
        for (const Motif* m : rederive) {
          auto it = realized.find(m->name);
          if (it != realized.end()) entry[m->name] = it->second;
        }

        const Pitch registerAnchor = localTmpl.startingPitch
            ? *localTmpl.startingPitch : *passageStart;
        std::optional<Pitch> pinned;
        if (localTmpl.parallel && lastChosenStart) pinned = lastChosenStart;
        const bool isFinal = i == (int)passTmpl.phrases.size() - 1;
        const float pulse = locus.plan->defaultPulse > 0.0f
            ? locus.plan->defaultPulse : 1.0f;

        struct PhraseCand {
          AnchorResult res;
          PhraseTemplate tmpl;
          MotifPool motifs;
        };
        std::vector<PhraseCand> cands;
        Randomizer candRng(locus.rng->rng());
        const int nCand = std::max(1, melodyProfile.search.phraseCandidates);
        for (int k = 0; k < nCand; ++k) {
          for (const auto& [n, f] : entry) realized[n] = f;
          for (const Motif* m : rederive) {
            const uint32_t s = m->generationSeed ? m->generationSeed
                                                 : candRng.rng();
            realized[m->name] = figure_transforms::apply(
                realized.at(*m->derivedFrom),
                m->transform.value_or(TransformOp::None),
                m->transformParam, s);
          }
          std::vector<const MelodicFigure*> figContents;
          figContents.reserve(localTmpl.figures.size());
          for (const auto& ft : localTmpl.figures) {
            if (ft.source == FigureSource::Reference) {
              auto rmIt = realized.find(ft.motifName);
              if (rmIt == realized.end())
                throw std::runtime_error(
                    "anchorMode harmonic: unresolved motif '" + ft.motifName +
                    "' (phrase '" + localTmpl.name + "')");
              figContents.push_back(&rmIt->second);
            } else if (ft.source == FigureSource::Locked && ft.lockedFigure) {
              figContents.push_back(&*ft.lockedFigure);
            } else {
              throw std::runtime_error(
                  "anchorMode harmonic requires reference/locked figures "
                  "(phrase '" + localTmpl.name + "')");
            }
          }
          PhraseCand pc;
          pc.tmpl = localTmpl;
          Randomizer selRng(candRng.rng());
          pc.res = select_anchors(
              pc.tmpl, figContents, sec.harmonyTimeline, sec.scale,
              phraseBeatCursor, float(sec.meter.beats_per_bar()), pulse,
              isFinal, pinned, registerAnchor, selRng,
              melodyProfile, priorTrack, sec.beats, wantAnchorLog);
          for (const Motif* m : rederive) pc.motifs[m->name] = realized.at(m->name);
          cands.push_back(std::move(pc));
        }
        std::vector<RankItem> items;
        for (const auto& pc : cands) items.push_back({pc.res.inBudget, pc.res.score});
        const size_t win = pick_top_k(items, melodyProfile.search.topK, candRng);
        PhraseCand& W = cands[win];

        if (wantAnchorLog) {
          for (size_t k = 0; k < cands.size(); ++k) {
            std::string line = "[phrase] '" + localTmpl.name + "' cand "
                             + std::to_string(k) + ":";
            for (const auto& [n, f] : cands[k].motifs) {
              line += " " + n + "=[";
              for (size_t u = 0; u < f.units.size(); ++u)
                line += (u ? "," : "") + std::to_string(f.units[u].step);
              line += "]";
            }
            char buf[96];
            std::snprintf(buf, sizeof buf, " score %.2f dep %d %s\n",
                          cands[k].res.score, cands[k].res.departures,
                          cands[k].res.inBudget ? "in" : "OVER");
            A.log += line + buf;
          }
          A.log += "[phrase] '" + localTmpl.name + "' CHOSEN cand "
                 + std::to_string(win) + "\n";
          A.log += W.res.log;
        }

        // Commit the winner.
        for (const auto& [n, f] : W.motifs) realized[n] = f;
        for (const auto& n : owned) committed.insert(n);
        for (const Motif* m : rederive) committed.insert(m->name);
        localTmpl = W.tmpl;
        priorTrack.insert(priorTrack.end(), W.res.notes.begin(), W.res.notes.end());
        A.score += W.res.score;
        A.inBudget = A.inBudget && W.res.inBudget;
        lastChosenStart = localTmpl.startingPitch;
      }

      Locus phraseLocus = locus.with_phrase(i);

      const PhraseStrategy* ps = &locus.kit->strategies.phrase(phraseTmpl.strategy.name);
      Phrase phrase = ps->compose_phrase(phraseLocus, localTmpl);

      for (const auto& figPtr : phrase.figures)
        phraseBeatCursor += figPtr->total_duration();
      passage.add_phrase(std::move(phrase));
    }

    if (harmonicMode) {
      A.score += passage_range_term(priorTrack, melodyProfile.critic);
      A.motifs = *realizedPool;
    }
  };

  if (!harmonicMode) {
    PassageAttempt A;
    run_attempt(A);
    return std::move(A.passage);
  }

  // ---- Passage level of the search: M whole passages, ranked, top-k dice. ----
  MotifPool& realized = *realizedPool;
  const MotifPool snapshot = realized;
  Randomizer passRng(locus.rng->rng());
  const int nPass = std::max(1, melodyProfile.search.passageCandidates);
  std::vector<PassageAttempt> attempts;
  attempts.reserve(size_t(nPass));
  for (int m = 0; m < nPass; ++m) {
    realized = snapshot;
    attempts.emplace_back();
    run_attempt(attempts.back());
  }
  std::vector<RankItem> items;
  for (const auto& a : attempts) items.push_back({a.inBudget, a.score});
  const size_t win = pick_top_k(items, melodyProfile.search.topK, passRng);
  realized = attempts[win].motifs;

  if (wantAnchorLog) {
    for (size_t m = 0; m < attempts.size(); ++m) {
      char buf[128];
      std::snprintf(buf, sizeof buf, "[passage] attempt %d: score %.2f %s\n",
                    int(m), attempts[m].score,
                    attempts[m].inBudget ? "in" : "OVER");
      std::cerr << buf;
    }
    std::cerr << "[passage] CHOSEN attempt " << win << "\n"
              << attempts[win].log;
  }
  return std::move(attempts[win].passage);
}

} // namespace mforce
