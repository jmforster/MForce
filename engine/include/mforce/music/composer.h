#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/strategy_registry.h"
#include "mforce/music/default_strategies.h"
#include "mforce/music/random_figure_builder.h"
#include "mforce/music/shape_strategies.h"
#include "mforce/music/phrase_strategies.h"
#include "mforce/music/wrapper_phrase_strategy.h"
#include "mforce/music/two_figure_phrase_strategy.h"
#include "mforce/music/elaborated_phrase_strategy.h"
#include "mforce/music/library_passage_strategy.h"
#include "mforce/music/passage_strategies.h"
#include "mforce/music/alternating_figure_strategy.h"
#include "mforce/music/phrase_aware_figure_strategy.h"
#include "mforce/music/period_passage_strategy.h"
#include "mforce/music/chord_progression_builder.h"
#include "mforce/music/chord_walker.h"
#include "mforce/music/harmony_timeline.h"
#include "mforce/music/voicing_selector.h"
#include "mforce/music/smooth_voicing_selector.h"
#include "mforce/music/voicing_profile_selector.h"
#include "mforce/music/static_voicing_profile_selector.h"
#include "mforce/music/random_voicing_profile_selector.h"
#include "mforce/music/drift_voicing_profile_selector.h"
#include "mforce/music/scripted_voicing_profile_selector.h"
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"
#include "mforce/music/anchor_selector.h"
#include "mforce/music/pitch_reader.h"
#include "mforce/music/realization_strategy.h"
#include "mforce/music/pitch_walker.h"
#include "mforce/music/compose.h"
#include "mforce/music/realize.h"
#include "mforce/core/randomizer.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>

namespace mforce {

// ---------------------------------------------------------------------------
// realize_motifs — free function
//
// Walks a PieceTemplate's Motif declarations and populates its realizedMotifs
// / realizedRhythms / realizedContours maps. Composer::setup_piece_ calls it
// on the plan. Non-const PieceTemplate& because it writes into the realized
// maps. Takes a Randomizer& for generation-seed fallback draws.
// ---------------------------------------------------------------------------
inline void realize_motifs(PieceTemplate& tmpl, Randomizer& rng) {
  tmpl.realizedMotifs.clear();
  tmpl.realizedRhythms.clear();
  tmpl.realizedContours.clear();

  // Pick a shared pulse for all generated motifs (phrase-level coherence).
  float sharedPulse = tmpl.defaultPulse;
  if (sharedPulse <= 0) {
    Randomizer pulseRng(rng.rng());
    static const float pulses[] = {0.5f, 0.5f, 1.0f, 1.0f, 1.0f, 1.5f, 2.0f};
    sharedPulse = pulses[pulseRng.int_range(0, 6)];
  }

  for (auto& motif : tmpl.motifs) {
    if (motif.is_rhythm()) {
      if (motif.userProvided || !motif.rhythm().pulses.empty()) {
        tmpl.realizedRhythms[motif.name] = motif.rhythm();
      } else {
        uint32_t s = motif.generationSeed ? motif.generationSeed : rng.rng();
        float totalBeats = 4.0f;
        if (motif.constraints && motif.constraints->totalBeats > 0)
          totalBeats = motif.constraints->totalBeats;
        PulseGenerator pgen(s);
        tmpl.realizedRhythms[motif.name] = pgen.generate(totalBeats, sharedPulse);
      }
    } else if (motif.is_contour()) {
      if (motif.userProvided || !motif.contour().steps.empty()) {
        tmpl.realizedContours[motif.name] = motif.contour();
      } else {
        uint32_t s = motif.generationSeed ? motif.generationSeed : rng.rng();
        int length = 4;
        if (motif.constraints && motif.constraints->maxNotes > 0)
          length = motif.constraints->maxNotes;
        StepGenerator sgen(s);
        tmpl.realizedContours[motif.name] = sgen.random_sequence(length);
      }
    } else {
      // Content-less motifs that NAME a parent are derivations — resolved
      // in phase 2 below, never random-generated.
      if (motif.figure().units.empty() && motif.derivedFrom) continue;
      if (motif.userProvided || !motif.figure().units.empty()) {
        tmpl.realizedMotifs[motif.name] = motif.figure();
      } else {
        uint32_t s = motif.generationSeed ? motif.generationSeed : rng.rng();
        FigureTemplate ft = motif.constraints.value_or(FigureTemplate{});
        if (ft.defaultPulse <= 0) ft.defaultPulse = sharedPulse;
        DefaultFigureStrategy figStrat;
        tmpl.realizedMotifs[motif.name] = figStrat.generate_figure(ft, s);
      }
    }
  }

  // Phase 2 — synthesize DERIVED figure motifs (walk spec 2026-09-21 §2):
  // a content-less motif with derivedFrom resolves through
  // figure_transforms::apply once its parent is realized. Chains resolve
  // over iterations; anything left standing (missing parent, cycle)
  // throws by name rather than silently random-generating. Seed:
  // generationSeed when authored, else drawn from the compose rng so
  // masterSeed varies the derivation per batch member.
  bool progress = true;
  while (progress) {
    progress = false;
    for (auto& motif : tmpl.motifs) {
      if (!(motif.is_figure() && motif.figure().units.empty()
            && motif.derivedFrom))
        continue;
      if (tmpl.realizedMotifs.count(motif.name)) continue;
      auto parentIt = tmpl.realizedMotifs.find(*motif.derivedFrom);
      if (parentIt == tmpl.realizedMotifs.end()) continue;
      const TransformOp op = motif.transform.value_or(TransformOp::None);
      const uint32_t s =
          motif.generationSeed ? motif.generationSeed : rng.rng();
      tmpl.realizedMotifs[motif.name] = figure_transforms::apply(
          parentIt->second, op, motif.transformParam, s);
      progress = true;
    }
  }
  for (const auto& motif : tmpl.motifs) {
    if (motif.is_figure() && motif.figure().units.empty() && motif.derivedFrom
        && !tmpl.realizedMotifs.count(motif.name)) {
      throw std::runtime_error(
          "realize_motifs: derived motif '" + motif.name +
          "' unresolved (missing parent '" + *motif.derivedFrom +
          "' or a derivation cycle)");
    }
  }
}

// ---------------------------------------------------------------------------
// Composer
//
// Walks a plan (the working copy of a request) and dispatches to the kit's
// strategies at each level of the hierarchy. Every strategy draws from the
// one Randomizer it owns, reached through Locus::rng. compose() in
// compose.h is the entry a host calls.
// ---------------------------------------------------------------------------
struct Composer {
  // The random source is seeded with seed + 200. Every composed piece's
  // draws come from this stream, so the offset is part of what a seed
  // composes.
  Composer(uint32_t seed, const ComposeKit& kit) : rng_(seed + 200), kit_(kit) {}

  // --- Top-level composition: writes the piece and the plan ---
  void compose(Piece& piece, PieceTemplate& plan) {
    setup_piece_(piece, plan);
    for (const auto& partTmpl : plan.parts) {
      if (partTmpl.role == PartRole::Harmony) continue;
      for (auto& sec : piece.sections) {
        compose_passage_(piece, plan, partTmpl, sec.name);
      }
    }
    realize_chord_parts_(piece, plan);
    realize_events(piece);
  }

  Passage compose_passage(Locus locus, const PassageTemplate& passTmpl) {
    PassageStrategy* s = &locus.kit->strategies.passage(passTmpl.strategy);
    // Plan phase — strategy refines the template (may mutate motif pool).
    PassageTemplate planned = s->plan_passage(locus, passTmpl);
    // Persist the planned template to the plan for regeneration / lock semantics.
    if (locus.sectionIdx >= 0 && locus.sectionIdx < (int)locus.piece->sections.size()
        && locus.partIdx >= 0 && locus.partIdx < (int)locus.plan->parts.size()) {
      const auto& sectionName = locus.piece->sections[locus.sectionIdx].name;
      locus.plan->parts[locus.partIdx].passages[sectionName] = planned;
    }
    // Compose phase — self-contained realization from the planned template.
    Passage passage = s->compose_passage(locus, planned);

    // Quantize the ending (#18). Central rather than repeated inside each
    // strategy: every passage returns through here, so a strategy written
    // tomorrow gets it for free and cannot forget it. endGrid=0 opts out.
    passage_anchors::grid_complete(passage, planned.endGrid);
    return passage;
  }

private:
  Randomizer rng_;
  const ComposeKit& kit_;

  // ---- Builds the piece's sections and parts, and realizes the motifs ----
  void setup_piece_(Piece& piece, PieceTemplate& tmpl) {
    // Key
    piece.key = Key::get(tmpl.keyName + " " +
        (tmpl.scaleName == "Minor" ? "Minor" : "Major"));

    // Sections
    piece.sections.clear();
    for (auto& sd : tmpl.sections) {
      // Section key (#13). `keyName` was accepted by no struct field and
      // dropped without a word; it now names the section's TONIC. The old
      // line realized scaleOverride at the PIECE tonic, so a section could
      // change its scale type but never its tonic — generalizing the tonic
      // here is a textual no-op when keyName is empty.
      const bool hasSectionKey = !sd.keyName.empty();
      std::string secTonic = tmpl.keyName;
      std::string secType  = sd.scaleOverride.empty() ? tmpl.scaleName
                                                      : sd.scaleOverride;
      if (hasSectionKey) {
        // "G" (type from scaleOverride/scaleName) or "G Minor" (type stated).
        auto sp = sd.keyName.find(' ');
        if (sp == std::string::npos) {
          secTonic = sd.keyName;
        } else {
          secTonic = sd.keyName.substr(0, sp);
          secType  = sd.keyName.substr(sp + 1);
        }
      }
      Scale secScale = (!hasSectionKey && sd.scaleOverride.empty())
        ? piece.key.scale
        : Scale::get(secTonic, secType);
      piece.add_section(Section(sd.name, sd.beats, tmpl.bpm, tmpl.meter, secScale));

      // Wire harmony
      Section& section = piece.sections.back();
      std::optional<ChordProgression> progression;
      if (sd.chordProgression) {
        progression = *sd.chordProgression;
      } else if (!sd.progressionName.empty()) {
        progression = ChordProgressionBuilder::build(sd.progressionName, sd.beats);
      }
      section.keyContexts = sd.keyContexts;

      // Desugar the section key into a beat-0 KeyContext. This is what makes
      // the melody path key-aware (realize_events gates on
      // keyContexts being non-empty), and it composes with LATER contexts:
      // "this section is in G, and modulates to D at beat 16" is legal.
      // A context already at beat <= 0 is two statements about the same beat,
      // which is an authoring error rather than a shorthand — say so.
      if (hasSectionKey) {
        for (const auto& kc : sd.keyContexts) {
          if (kc.beat <= 0.0f) {
            throw std::runtime_error(
                "Section '" + sd.name + "': keyName '" + sd.keyName +
                "' conflicts with a keyContext at beat " +
                std::to_string(kc.beat) + " — state the key one way or the "
                "other (drop keyName, or move the context off beat 0)");
          }
        }
        KeyContext ctx;
        ctx.beat = 0.0f;
        ctx.key = Key::get(secTonic + " " +
                           (secType == "Minor" ? "Minor" : "Major"));
        // Only an exotic scale type needs to be carried explicitly; for
        // Major/Minor the key's own scale already IS secScale.
        if (secScale.scaleDef != ctx.key.scale.scaleDef) ctx.scaleOverride = secScale;
        section.keyContexts.insert(section.keyContexts.begin(), ctx);
      }

      // The section's harmony is its timeline: one segment holding the
      // authored or named progression.
      if (progression) {
        section.harmonyTimeline.set_segment(
            0.0f, sd.beats, std::move(*progression), "authored");
      }
    }

    // Parts
    piece.parts.clear();
    for (auto& pt : tmpl.parts) {
      Part p;
      p.name = pt.name;
      p.instrumentType = pt.name;  // the ensemble maps instrumentType to an instrument
      piece.parts.push_back(std::move(p));
    }

    // Realize motifs
    realize_motifs(tmpl, rng_);
  }

  void realize_chord_parts_(Piece& piece, const PieceTemplate& tmpl) {
    for (int pi = 0; pi < (int)tmpl.parts.size(); ++pi) {
      const auto& partTmpl = tmpl.parts[pi];
      if (partTmpl.role != PartRole::Harmony) continue;

      Part* part = nullptr;
      for (auto& p : piece.parts) {
        if (p.name == partTmpl.name) { part = &p; break; }
      }
      if (!part) continue;
      RealizationStrategy* blockStrat = &kit_.strategies.realization("block");

      for (int si = 0; si < (int)piece.sections.size(); ++si) {
        const auto& sec = piece.sections[si];
        const float beatOffset = section_start_beat(piece.sections, std::size_t(si));

        auto passIt = partTmpl.passages.find(sec.name);
        const PassageTemplate* passTmpl = (passIt != partTmpl.passages.end())
            ? &passIt->second : nullptr;
        ChordAccompanimentConfig cfg;
        if (passTmpl && passTmpl->chordConfig) cfg = *passTmpl->chordConfig;

        // Harmony source: a passage-local progression wins over the section
        // timeline, so two passages in one section can carry different
        // chords. The section's own timeline is left untouched — melody
        // chord-tone lookups still read it.
        HarmonyTimeline localTimeline;
        const HarmonyTimeline* timeline = &sec.harmonyTimeline;
        if (passTmpl && passTmpl->chordProgression) {
          localTimeline.set_segment(0.0f, sec.beats,
                                    *passTmpl->chordProgression, "passage");
          timeline = &localTimeline;
        }
        if (timeline->empty()) continue;

        // Rhythm-pattern source. Present = "pattern" mode: the bar pattern
        // strikes and each strike samples the timeline. Absent = "span" mode:
        // one event per authored chord, at the duration the progression
        // already gives it. Span is the default because a progression carries
        // its own durations; a rhythmPattern RE-articulates them.
        const RhythmPattern* rp = (passTmpl && passTmpl->rhythmPattern)
            ? &*passTmpl->rhythmPattern : nullptr;

        // Optional VoicingSelector (empty name = inversion/spread path).
        VoicingSelector* selector = nullptr;
        if (passIt != partTmpl.passages.end()
            && !passIt->second.voicingSelector.empty()) {
          selector = &kit_.voicings.selector(passIt->second.voicingSelector);
        }

        // VoicingProfileSelector: produces a VoicingProfile per chord.
        // Empty name = "static" (baseline profile unchanged every chord).
        // Only built when a VoicingSelector is in play.
        std::unique_ptr<VoicingProfileSelector> profileSelector;
        if (selector && passIt != partTmpl.passages.end()) {
          profileSelector = kit_.voicings.make_profile_selector(
              passIt->second.voicingProfileSelector);
          if (auto* sw = dynamic_cast<StaticVoicingProfileSelector*>(
                             profileSelector.get())) {
            sw->configure(passIt->second.voicingProfile);
          }
          profileSelector->configure_from_json(
              passIt->second.voicingProfileSelectorConfig);
          uint32_t seed = tmpl.masterSeed ^ 0x566F6953u
                        ^ (uint32_t(si) * 100u + uint32_t(pi));
          profileSelector->reset(seed);
        }

        float beatsPerBar = float(sec.meter.beats_per_bar());
        int totalBars = int(sec.beats / beatsPerBar);

        const Chord* prevChord = nullptr;
        int chordIdx = 0;

        // One voicing + realization path, shared by both emission modes, so
        // span mode is not a second-class citizen of the voicing tier.
        auto emit_chord = [&](const ScaleChord& sc, float pos, float dur) {
          float beatInPassage = pos - beatOffset;
          int bar = int(beatInPassage / beatsPerBar);
          // Chords follow the section's key contexts, the way melody
          // realization has since #6 stage 3. Without them active_scale_at
          // returns the section scale, so this is a strict no-op for every
          // template that does not modulate — and with them, one authored
          // progression walks through keys (Matt's Bruckner pedal).
          Scale activeScale = sec.active_scale_at(beatInPassage);
          Chord chord;
          if (sc.pin) {
            // An authored VoicingPin outranks any selector: this chord's
            // voicing is part of its identity (cadential role), not a
            // preference for the motion-minimizer to trade away. The pinned
            // chord still becomes prevChord below, so the NEXT chord's
            // selector smooths away from the pinned voicing.
            const VoicingPin& pin = *sc.pin;
            int oct = pin.octave ? *pin.octave : cfg.octave;
            chord = sc.resolve(activeScale, oct, dur, pin.inversion,
                               pin.spread);
            if (pin.topTone >= 0 && !chord.pitches.empty()) {
              // Force the requested chord tone on top, doubling it in the
              // first octave above the current top voice if the pinned
              // voicing does not already end on it. Doubling is the point —
              // a close triad can never end on its bass tone otherwise.
              Chord ref = sc.resolve(activeScale, oct, dur, pin.topTone, 0);
              if (!ref.pitches.empty()) {
                float want = ref.pitches.front().note_number();
                float top = chord.pitches.back().note_number();
                int pcDiff = ((int(std::lround(top - want)) % 12) + 12) % 12;
                if (pcDiff != 0) {
                  float nn = want;
                  while (nn < top + 1.0f) nn += 12.0f;
                  chord.pitches.push_back(Pitch::from_note_number(nn));
                }
              }
            }
          } else if (selector) {
            float beatInBar = beatInPassage - bar * beatsPerBar;
            VoicingProfile profile = profileSelector
                ? profileSelector->profile_for_chord(
                      chordIdx, beatInBar, beatInPassage)
                : passIt->second.voicingProfile;
            VoicingRequest req{sc, &activeScale, cfg.octave, dur,
                               prevChord, std::nullopt,
                               profile,
                               passIt->second.voicingDictionary};
            chord = selector->select(req);
          } else {
            chord = sc.resolve(activeScale, cfg.octave, dur,
                               cfg.inversion, cfg.spread);
          }
          RealizationRequest realReq{chord, pos, dur, bar + 1, nullptr};
          blockStrat->realize(realReq, part->elementSequence);
          prevChord = &part->elementSequence.elements.back().chord();
          ++chordIdx;
        };

        if (rp) {
          for (int bar = 0; bar < totalBars; ++bar) {
            float barStart = beatOffset + bar * beatsPerBar;
            const auto& pattern = rp->pattern_for_bar(bar + 1);

            float pos = barStart;
            for (float dur : pattern) {
              if (dur < 0) {
                pos += (-dur);
                continue;
              }
              const ScaleChord* sc = timeline->chord_at(pos - beatOffset);
              if (sc) emit_chord(*sc, pos, dur);
              pos += dur;
            }
          }
        } else {
          // Span mode: walk the timeline's own chord boundaries. Chords are
          // emitted as authored, including any that overrun the section —
          // clipping an authored cadence chord would be a silent edit.
          for (const auto& seg : timeline->segments) {
            float b = seg.startBeat;
            for (int ci = 0; ci < seg.progression.count(); ++ci) {
              float dur = seg.progression.pulses.get(ci);
              if (dur > 0.0f) {
                emit_chord(seg.progression.chords.get(ci), beatOffset + b, dur);
              }
              b += dur;
            }
          }
        }
      }
    }
  }

  // ---- The passage composed for a section the part's template omits ------
  Passage generate_default_passage_(Piece& piece, PieceTemplate& tmpl,
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

    Locus locus{&piece, &tmpl, &kit_, &rng_, 0, 0};
    return this->compose_passage(locus, passTmpl);
  }

  // ---- Per-passage dispatcher: one part's passage for one section --------
  void compose_passage_(Piece& piece, PieceTemplate& tmpl,
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
    Section* section = nullptr;
    for (auto& s : piece.sections) {
      if (s.name == sectionName) { section = &s; break; }
    }

    Passage passage;
    if (passIt != partTmpl.passages.end()) {
      // Template-driven: dispatch through the registry. Build a Locus that
      // names this passage's position.
      int sectionIdx = -1;
      for (int i = 0; i < (int)piece.sections.size(); ++i) {
        if (piece.sections[i].name == sectionName) { sectionIdx = i; break; }
      }
      int partIdx = -1;
      for (int i = 0; i < (int)piece.parts.size(); ++i) {
        if (piece.parts[i].name == partTmpl.name) { partIdx = i; break; }
      }
      Locus locus{&piece, &tmpl, &kit_, &rng_, sectionIdx, partIdx};
      if (section) {
        locus.harmony = &section->harmonyTimeline;
      }

      // If strategy is Melody-only and no harmony exists yet, generate via ChordWalker.
      const PieceTemplate::SectionTemplate* sd = nullptr;
      for (const auto& s : tmpl.sections) {
        if (s.name == sectionName) { sd = &s; break; }
      }
      PassageStrategy* strat = &kit_.strategies.passage(passIt->second.strategy);
      if (strat->scope() == StrategyScope::Melody
          && section && section->harmonyTimeline.empty()
          && sd && !sd->styleName.empty()) {
        auto style = StyleTable::load_by_name(sd->styleName);
        WalkConstraint wc;
        wc.startChord = ScaleChord{0, 0, &ChordDef::get("Major")};
        wc.totalBeats = sd->beats;
        auto prog = ChordWalker::walk(style, wc, tmpl.masterSeed + sectionIdx * 1000);
        section->harmonyTimeline.set_segment(
            0.0f, sd->beats, prog, "chord_walker");
      }

      passage = compose_passage(locus, passIt->second);
      // Pin this passage's scale if the template asks for it. Doubles as the
      // "this part does not follow the section's key contexts" switch, which
      // is what a pedal needs.
      if (!passIt->second.scaleOverride.empty()) {
        passage.scaleOverride = Scale::get(tmpl.keyName,
                                           passIt->second.scaleOverride);
      }
    } else {
      // No template for this section — fallback.
      passage = generate_default_passage_(piece, tmpl, scale);
    }

    part->passages[sectionName] = std::move(passage);
  }
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
    Locus locus, const FigureTemplate& ft) {
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
    // For now, keep it simple and direct:
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
    Locus locus, const FigureTemplate& ft) {
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
    Locus locus, const FigureTemplate& ft) {
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

// ============================================================================
// Out-of-line definition of DefaultFigureStrategy::compose_figure. It looks
// up the shape strategies in the kit, which needs ComposeKit complete.
// ============================================================================

inline MelodicFigure DefaultFigureStrategy::compose_figure(
    Locus locus, const FigureTemplate& figTmpl) {
  const Scale& scale = locus.piece->sections[locus.sectionIdx].scale;
  // Use the figure's seed for reproducibility
  uint32_t figSeed = figTmpl.seed ? figTmpl.seed : locus.rng->rng();

  switch (figTmpl.source) {
    case FigureSource::Locked:
      if (figTmpl.lockedFigure) return *figTmpl.lockedFigure;
      {
        RandomFigureBuilder rfb(locus.rng->rng());
        Constraints fc; fc.length = 1.0f;
        return rfb.build_singleton(fc);  // fallback
      }

    case FigureSource::Literal: {
      // Convert the user-authored note list into a MelodicFigure. Each
      // LiteralNote becomes one FigureUnit whose `step` is the delta (in
      // scale degrees) from the previous note (or from the prior cursor
      // for the first note). Duration passes through.
      MelodicFigure fig;
      if (figTmpl.literalNotes.empty()) return fig;

      auto absoluteDeg = [&](const Pitch& p) {
        int d = DefaultPhraseStrategy::degree_in_scale(p, scale);
        return p.octave * scale.length() + d;
      };

      const Pitch cursorPitch = ::mforce::piece_utils::pitch_before(locus);
      int prevDeg = absoluteDeg(cursorPitch);
      for (auto& ln : figTmpl.literalNotes) {
        FigureUnit u;
        u.duration = ln.duration;
        if (ln.rest) {
          u.rest = true;
          u.step = 0;
        } else {
          if (!ln.pitch) continue;
          int d = absoluteDeg(*ln.pitch);
          u.step = d - prevDeg;
          prevDeg = d;
        }
        fig.units.push_back(u);
      }
      return fig;
    }

    case FigureSource::Reference: {
      auto it = locus.plan->realized_motifs().find(figTmpl.motifName);
      if (it != locus.plan->realized_motifs().end()) return it->second;
      return generate_figure(figTmpl, figSeed);
    }

    case FigureSource::Transform: {
      auto it = locus.plan->realized_motifs().find(figTmpl.motifName);
      MelodicFigure base = (it != locus.plan->realized_motifs().end())
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
          // Only shapes with real strategy logic dispatch here. Others
          // (ScalarRun, RepeatedNote, HeldNote, TriadicOutline, NeighborTone,
          // LeapAndFill, ScalarReturn, Anacrusis, Zigzag, Fanfare, Sigh,
          // Suspension, Cambiata) fall through to generate_figure — their
          // shape intent is available via shape_figures:: at call sites
          // that want it.
          case FigureShape::CadentialApproach: shapeName = "shape_cadential_approach"; break;
          case FigureShape::Skipping:          shapeName = "shape_skipping"; break;
          case FigureShape::Stepping:          shapeName = "shape_stepping"; break;
          default:                             shapeName = nullptr; break;
        }
        if (shapeName) {
          FigureStrategy* s = locus.kit->strategies.find_figure(shapeName);
          if (s) {
            FigureTemplate shapeArg = figTmpl;
            shapeArg.seed = figSeed;
            fig = s->compose_figure(locus, shapeArg);
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
// Out-of-line definition of DefaultPassageStrategy::compose_passage. It
// looks up phrase strategies in the kit, which needs ComposeKit complete.
// ============================================================================

inline Passage DefaultPassageStrategy::compose_passage(
    Locus locus, const PassageTemplate& passTmpl) {
  // The comment that used to sit here said "should not happen — loader
  // refuses templates without startingPitch" and returned an EMPTY passage.
  // The loader does not refuse them: patches/template_shaped_test.json has
  // no passage startingPitch and has therefore been rendering SILENCE, with
  // no diagnostic, for as long as it has existed (comp #9 harness found it).
  // The information is usually present one level down, so use it; and when
  // it genuinely is not, refuse by name rather than emit nothing.
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

  // Harmonic anchor mode (walk specs 2026-09-21 §3, 2026-09-22 walk3):
  // requires a genre profile; the old in-code constants are gone.
  const bool harmonicMode = passTmpl.anchorMode == "harmonic";
  const bool wantAnchorLog =
      harmonicMode && std::getenv("MFORCE_ANCHOR_LOG") != nullptr;
  MelodyProfile melodyProfile;
  if (harmonicMode) {
    if (passTmpl.melodyProfile.empty())
      throw std::runtime_error(
          "anchorMode harmonic requires melodyProfile (passage '" +
          passTmpl.name + "')");
    melodyProfile = MelodyProfile::load_by_name(passTmpl.melodyProfile);
  }

  // Walk3 best-of-N rerolls derived motifs and commits the winners, so it
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
  // once with no extra draws (byte-identical to the pre-walk3 loop).
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
        // Structural fix for period parallelism — previously a parallel
        // consequent depended on the running pitch happening to land back on
        // the antecedent's opening pitch.
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
      // Walk3 §3 phrase level: N candidates, each rerolling the derived
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

      PhraseStrategy* ps = &locus.kit->strategies.phrase(phraseTmpl.strategy);
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

  // ---- Walk3 §3 passage level: M whole passages, ranked, top-k dice. ----
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

// ============================================================================
// Out-of-line definition of DefaultPhraseStrategy::compose_phrase. It looks
// up the default figure strategy in the kit, which needs ComposeKit complete.
// ============================================================================

inline Phrase DefaultPhraseStrategy::compose_phrase(
    Locus locus, const PhraseTemplate& phraseTmpl) {
  Phrase phrase;

  const Scale& scale = locus.piece->sections[locus.sectionIdx].scale;

  if (phraseTmpl.startingPitch) {
    phrase.startingPitch = *phraseTmpl.startingPitch;
  } else {
    phrase.startingPitch = ::mforce::piece_utils::pitch_before(locus);
  }

  // Intra-phrase running cursor. Starts at the phrase's starting pitch
  // and advances through each figure's net_step() as figures realize.
  // Figure strategies will construct their own cursor via pitch_before(),
  // but since the phrase isn't published yet we pass a synthetic figure
  // template startingPitch for Literal path consumers.
  PitchReader runningReader(scale);
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

    // MelodicFunction-driven shape selection.
    FigureTemplate figTmpl = phraseTmpl.figures[i];
    if (phraseTmpl.function != MelodicFunction::Free
        && figTmpl.source == FigureSource::Generate
        && figTmpl.shape == FigureShape::Free) {
      figTmpl.shape = DefaultFigureStrategy::choose_shape(
          phraseTmpl.function, i, numFigs, locus.rng->rng(),
          phraseTmpl.cadentialArrival == "approach");
    }

    // Dispatch to the figure level. DefaultFigureStrategy's Literal path
    // reads the cursor via pitch_before(locus), which walks the phrase/
    // passage structure so far. The phrase being built is not in the piece
    // yet, so pitch_before would see an empty passage and fall back to the
    // passage template's startingPitch, which is wrong mid-phrase. A Literal
    // figure is therefore realized here against runningReader, the cursor
    // advanced by the figures before it. Other sources do not read the
    // cursor and go to the default figure strategy.
    Locus figLocus = locus.with_figure(i);
    FigureStrategy* fs = &locus.kit->strategies.figure("default_figure");
    MelodicFigure fig;
    if (figTmpl.source == FigureSource::Literal) {
      // Inline cursor-aware Literal realization using the runningReader.
      const Pitch cursorPitch = runningReader.get_pitch();
      auto absoluteDeg = [&](const Pitch& p) {
        int d = DefaultPhraseStrategy::degree_in_scale(p, scale);
        return p.octave * scale.length() + d;
      };
      int prevDeg = absoluteDeg(cursorPitch);
      for (auto& ln : figTmpl.literalNotes) {
        FigureUnit u;
        u.duration = ln.duration;
        if (ln.rest) { u.rest = true; u.step = 0; }
        else {
          if (!ln.pitch) continue;
          int d = absoluteDeg(*ln.pitch);
          u.step = d - prevDeg;
          prevDeg = d;
        }
        fig.units.push_back(u);
      }
    } else {
      fig = fs->compose_figure(figLocus, figTmpl);
    }

    // Pull this figure's connector from the template (if any). Default-
    // constructed FCs have leadStep=0/elide=0/adjust=0. Push to
    // phrase.connectors alongside the figure to keep the
    // figures.size() == connectors.size() invariant. The cursor advance
    // from leadStep is applied at realize time (see
    // realize_events), NOT here. Figures are not mutated.
    FigureConnector fc{};
    if (i < int(phraseTmpl.connectors.size()) && phraseTmpl.connectors[i]) {
      fc = *phraseTmpl.connectors[i];
    }

    // Advance running cursor by the FC's leadStep (used by the Literal-
    // figure path's pitch_before queries) and then by the figure's net
    // step. Under the new model, fig.units[0].step is 0 by convention, so
    // fig.net_step() no longer absorbs leadStep — we add it explicitly.
    runningReader.step(fc.leadStep);
    runningReader.step(fig.net_step());

    phrase.add_melodic_figure_with_connector(std::move(fig), fc);
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
      apply_cadence(phrase, phraseTmpl, scale);
    }
  }

  return phrase;
}

// ---------------------------------------------------------------------------
// chord_walk — the running-pitch advance shared by the two chord-driven
// passage strategies (alternating_figure, phrase_aware_figure).
//
// A ChordFigure's steps are chord-TONE indices, not scale degrees, so the
// running cursor cannot simply be stepped: it has to be re-seated on the
// chord's resolved tones. Both strategies had a verbatim copy of this;
// factored out when the phrase_aware norm-breaker arm (32 chords) crashed
// with "Unknown PitchDef offset: -3" — AFS crashes identically on the same
// progression, so this is a pre-existing hole, not a new one. The walk is an
// unbounded random walk in register: nothing stopped it descending past note
// number 0, where Pitch::from_note_number does `n % 12` on a negative and
// hands PitchDef::get an offset that does not exist.
//
// The old walk did not agree with realization, and that is what made the
// crash possible AND made chord-driven cadences land on the wrong degree:
//
//   * it applied the figure's NET step in ONE step_chord_tone-style hop,
//     where realize_events applies EVERY unit's step in turn.
//     pitch_walker.h says in as many words that step_chord_tone is not
//     linear over walks, so one hop of n is not n hops of one;
//   * it resolved the chord at the RUNNING octave, where realization
//     resolves at kBaseOctave. Resolving at the running octave makes the
//     candidate window follow the cursor, so a descending bias compounds
//     instead of being re-anchored — that is the runaway that reached
//     note number -3.
//
// So the cursor the strategy used to compute a cadential approach was not
// the pitch the listener would hear, and the approach was therefore
// computed from the wrong degree. This walks exactly what realization
// walks. It changes AFS output too; that is a fix, not a regression, and
// the cadence hit-rate measurement is the evidence.
// ---------------------------------------------------------------------------
namespace chord_walk {

// Advance `reader` through one chord-tone figure, unit by unit, mirroring
// realize_events's ChordFigure branch. `sectionScale` and
// `baseOctave` must be the ones realization uses.
inline void advance(PitchReader& reader, const ScaleChord& chord,
                    const Scale& sectionScale, int baseOctave,
                    const MelodicFigure& fig) {
  auto resolved = chord.resolve(sectionScale, baseOctave);
  if (resolved.pitches.empty()) return;
  float nn = reader.get_note_number();
  for (const auto& u : fig.units) {
    nn = step_chord_tone(nn, u.step, resolved);
  }
  reader.set_pitch(Pitch::from_note_number(nn));
}

} // namespace chord_walk

// ---------------------------------------------------------------------------
// Out-of-line definition of AlternatingFigureStrategy::compose_passage.
//
// It looks up the default figure strategy in the kit, which needs
// ComposeKit complete.
// ---------------------------------------------------------------------------
inline Passage AlternatingFigureStrategy::compose_passage(
    Locus locus, const PassageTemplate& pt) {

  const Scale& scale = locus.piece->sections[locus.sectionIdx].scale;
  const ChordProgression chordProg =
      locus.piece->sections[locus.sectionIdx].harmonyTimeline.progression();
  if (chordProg.count() == 0) {
    throw std::runtime_error("AFS: no chord progression on section");
  }
  if (pt.phrases.empty() || pt.phrases[0].figures.size() < 2) {
    throw std::runtime_error(
        "AFS: need 1 phrase with at least 2 figure templates (A and B)");
  }

  const auto& figTemplateA = pt.phrases[0].figures[0];
  const auto& figTemplateB = pt.phrases[0].figures[1];

  Phrase phrase;
  if (pt.phrases[0].startingPitch) {
    phrase.startingPitch = *pt.phrases[0].startingPitch;
  } else {
    phrase.startingPitch = ::mforce::piece_utils::pitch_before(locus.with_phrase(0));
  }

  PitchReader runningReader(scale);
  runningReader.set_pitch(phrase.startingPitch);

  FigureStrategy* fs = &locus.kit->strategies.figure("default_figure");

  for (int ci = 0; ci < chordProg.count(); ++ci) {
    bool isA = (ci % 2 == 0);
    const auto& ft = isA ? figTemplateA : figTemplateB;

    FigureTemplate adjusted = ft;
    adjusted.totalBeats = chordProg.pulses.get(ci);

    if (!isA && adjusted.figureCadenceType > 0) {
      int bIndex = ci / 2;
      adjusted.figureCadenceType = (bIndex % 2 == 0) ? 1 : 2;
    }

    MelodicFigure rawFig = fs->compose_figure(locus.with_phrase(0).with_figure(ci), adjusted);

    if (isA) {
      chord_walk::advance(runningReader, chordProg.chords.get(ci), scale,
                          4 /*kBaseOctave*/, rawFig);

      auto cf = std::make_unique<ChordFigure>();
      cf->units = rawFig.units;
      phrase.add_figure(std::move(cf));
    } else {
      runningReader.step(rawFig.net_step());
      phrase.add_melodic_figure(std::move(rawFig));
    }
  }

  Passage passage;
  passage.add_phrase(std::move(phrase));
  return passage;
}

// ---------------------------------------------------------------------------
// Out-of-line definition of PhraseAwareFigureStrategy::compose_passage.
// Same placement rationale as AlternatingFigureStrategy above.
//
// Comp backlog #7 — the chord-driven strategy that respects phrase
// boundaries. See docs/superpowers/specs/2026-08-10-phrase-aware-cadence-design.md.
// ---------------------------------------------------------------------------
inline Passage PhraseAwareFigureStrategy::compose_passage(
    Locus locus, const PassageTemplate& pt) {

  const Scale& scale = locus.piece->sections[locus.sectionIdx].scale;

  // Passage-local progression wins over the section's timeline, so two
  // passages in one section can carry different harmony.
  const ChordProgression* prog = nullptr;
  ChordProgression sectionProg;
  if (pt.chordProgression && pt.chordProgression->count() > 0) {
    prog = &*pt.chordProgression;
  } else {
    sectionProg = locus.piece->sections[locus.sectionIdx].harmonyTimeline.progression();
    if (sectionProg.count() > 0) prog = &sectionProg;
  }
  if (!prog) {
    throw std::runtime_error(
        "phrase_aware_figure: no chord progression on passage or section");
  }

  // Collect the phrase templates we will actually use.
  std::vector<const PhraseTemplate*> phraseTmpls;
  for (const auto& ph : pt.phrases) {
    if (ph.locked) continue;
    if (ph.figures.empty()) {
      throw std::runtime_error(
          "phrase_aware_figure: phrase '" + ph.name + "' has no figure templates");
    }
    phraseTmpls.push_back(&ph);
  }
  if (phraseTmpls.empty()) {
    throw std::runtime_error("phrase_aware_figure: passage has no usable phrases");
  }

  // Beat budgets: explicit totalBeats, else the sum of the figures'.
  std::vector<float> budgets;
  for (const auto* ph : phraseTmpls) {
    float b = ph->totalBeats;
    if (!(b > 0.0f)) {
      b = 0.0f;
      for (const auto& ft : ph->figures) b += ft.totalBeats;
    }
    budgets.push_back(b);
  }
  std::vector<float> chordBeats;
  for (int ci = 0; ci < prog->count(); ++ci) chordBeats.push_back(prog->pulses.get(ci));

  const std::vector<int> bounds = partition_chords(budgets, chordBeats);
  const int phrasesUsed = int(bounds.size()) - 1;

  Passage passage;

  FigureStrategy* fs = &locus.kit->strategies.figure("default_figure");

  // Passage-level running cursor. Carried across phrases so a non-parallel
  // phrase continues where the previous one stopped — the same continuity
  // rule DefaultPassageStrategy uses.
  Pitch passageStart = pt.startingPitch
      ? *pt.startingPitch
      : ::mforce::piece_utils::pitch_before(locus.with_phrase(0));
  PitchReader runningReader(scale);
  runningReader.set_pitch(passageStart);

  for (int p = 0; p < phrasesUsed; ++p) {
    const PhraseTemplate& ph = *phraseTmpls[p];
    const int c0 = bounds[p];
    const int c1 = bounds[p + 1];

    Phrase phrase;
    if (ph.startingPitch) {
      phrase.startingPitch = *ph.startingPitch;
      runningReader.set_pitch(phrase.startingPitch);
    } else if (ph.parallel) {
      phrase.startingPitch = passageStart;
      runningReader.set_pitch(phrase.startingPitch);
    } else {
      phrase.startingPitch = runningReader.get_pitch();
    }

    const int cadType = ph.cadenceType;

    for (int ci = c0; ci < c1; ++ci) {
      const int j = ci - c0;                       // position WITHIN the phrase
      const bool isLast = (ci == c1 - 1);
      const bool isCadence = isLast && cadType > 0;

      // Body texture alternates on j, not on the global chord index. The
      // cadence figure is always melodic: apply_cadence accounts degrees by
      // summing net_step() in scale steps, so a chord-tone final figure
      // would desynchronise its arithmetic.
      const bool useA = !isCadence && (j % 2 == 0);
      const int tmplIdx = (useA || ph.figures.size() < 2) ? 0 : 1;

      FigureTemplate adjusted = ph.figures[std::min<size_t>(tmplIdx, ph.figures.size() - 1)];
      adjusted.totalBeats = prog->pulses.get(ci);
      if (isCadence) {
        adjusted.figureCadenceType = cadType;
        if (adjusted.shape == FigureShape::Free) {
          adjusted.shape = FigureShape::CadentialApproach;
        }
      } else {
        adjusted.figureCadenceType = 0;
      }

      // Degree the cursor has actually reached, tracked through both chord-
      // tone and scale-step figures. apply_cadence cannot recompute this
      // (it sums net_step() assuming every figure steps in scale degrees,
      // which a ChordFigure does not), so we hand it the real answer.
      const Pitch preFigurePitch = runningReader.get_pitch();
      const int preFigureDeg =
          DefaultPhraseStrategy::degree_in_scale(preFigurePitch, scale);

      MelodicFigure rawFig =
          fs->compose_figure(locus.with_phrase(p).with_figure(j), adjusted);

      if (isCadence) {
        // Land the arrival on the phrase's cadence target, rewriting the
        // figure's tail into approach + settled arrival.
        int target = ph.cadenceTarget >= 0 ? ph.cadenceTarget
                                           : default_cadence_target(cadType);
        const int len = scale.length();
        target = ((target % len) + len) % len;
        DefaultPhraseStrategy::rebuild_cadential_tail(rawFig, preFigureDeg,
                                                      target, len, 1.0f);
        runningReader.set_pitch(preFigurePitch);
        runningReader.step(rawFig.net_step());
        std::cerr << "phrase_aware_figure: phrase " << p
                  << " chords [" << c0 << "," << c1 << ") cadType " << cadType
                  << " target deg " << target
                  << " startDeg " << preFigureDeg
                  << " arrivalDeg " << (preFigureDeg + rawFig.net_step())
                  << " notes " << rawFig.note_count()
                  << " finalDur " << (rawFig.units.empty() ? 0.0f
                                      : rawFig.units.back().duration)
                  << "\n";
        phrase.add_melodic_figure(std::move(rawFig));
        continue;
      }

      if (useA) {
        // Chord-tone figure: walk the running pitch through the chord's
        // tones exactly as realization will (shared with AFS).
        chord_walk::advance(runningReader, prog->chords.get(ci), scale,
                            4 /*kBaseOctave*/, rawFig);

        auto cf = std::make_unique<ChordFigure>();
        cf->units = rawFig.units;
        phrase.add_figure(std::move(cf));
      } else {
        runningReader.step(rawFig.net_step());
        phrase.add_melodic_figure(std::move(rawFig));
      }
    }

    // NOTE: no post-loop apply_cadence call. The arrival is rewritten in
    // place above, with the cursor's true degree — apply_cadence would
    // recompute that degree by summing net_step() over the preceding
    // figures, which is wrong here because the body figures are
    // ChordFigures stepping in chord-tone indices, not scale degrees.

    passage.add_phrase(std::move(phrase));
  }

  return passage;
}

} // namespace mforce
