#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/strategy_registry.h"
#include "mforce/music/default_strategies.h"
#include "mforce/music/shape_strategies.h"
#include "mforce/music/phrase_strategies.h"
#include "mforce/music/alternating_figure_strategy.h"
#include "mforce/music/period_passage_strategy.h"
#include "mforce/music/chord_progression_builder.h"
#include "mforce/music/chord_walker.h"
#include "mforce/music/harmony_timeline.h"
#include "mforce/music/voicing_selector.h"
#include "mforce/music/smooth_voicing_selector.h"
#include "mforce/music/voicing_profile_selector.h"
#include "mforce/music/static_voicing_profile_selector.h"
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"
#include "mforce/music/pitch_reader.h"
#include "mforce/music/realization_strategy.h"
#include "mforce/music/dynamic_state.h"
#include "mforce/music/pitch_walker.h"
#include "mforce/music/rng.h"
#include "mforce/core/randomizer.h"
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>

namespace mforce {

// ---------------------------------------------------------------------------
// realize_motifs — free function
//
// Walks a PieceTemplate's Motif declarations and populates its realizedMotifs
// / realizedRhythms / realizedContours maps. Called by Composer::setup_piece_
// after template load. Non-const PieceTemplate& because it writes into the
// realized maps. Takes a Randomizer& for generation-seed fallback draws.
//
// Lifted verbatim from the pre-refactor ClassicalComposer::realize_motifs and
// its port to Composer::realize_motifs_. No behavioral change.
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
}

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
    auto& reg = StrategyRegistry::instance();
    reg.register_figure (std::make_unique<DefaultFigureStrategy>());
    reg.register_phrase (std::make_unique<DefaultPhraseStrategy>());
    reg.register_passage(std::make_unique<DefaultPassageStrategy>());

    // Shape strategies (Phase 2) — all Figure-level
    reg.register_figure(std::make_unique<ShapeScalarRunStrategy>());
    reg.register_figure(std::make_unique<ShapeRepeatedNoteStrategy>());
    reg.register_figure(std::make_unique<ShapeHeldNoteStrategy>());
    reg.register_figure(std::make_unique<ShapeCadentialApproachStrategy>());
    reg.register_figure(std::make_unique<ShapeTriadicOutlineStrategy>());
    reg.register_figure(std::make_unique<ShapeNeighborToneStrategy>());
    reg.register_figure(std::make_unique<ShapeLeapAndFillStrategy>());
    reg.register_figure(std::make_unique<ShapeScalarReturnStrategy>());
    reg.register_figure(std::make_unique<ShapeAnacrusisStrategy>());
    reg.register_figure(std::make_unique<ShapeZigzagStrategy>());
    reg.register_figure(std::make_unique<ShapeFanfareStrategy>());
    reg.register_figure(std::make_unique<ShapeSighStrategy>());
    reg.register_figure(std::make_unique<ShapeSuspensionStrategy>());
    reg.register_figure(std::make_unique<ShapeCambiataStrategy>());
    reg.register_figure(std::make_unique<ShapeSkippingStrategy>());
    reg.register_figure(std::make_unique<ShapeSteppingStrategy>());

    // Phrase strategies (Phase 3)
    reg.register_phrase(std::make_unique<PeriodPhraseStrategy>());
    reg.register_phrase(std::make_unique<SentencePhraseStrategy>());

    // Voicing selectors (Phase 4)
    VoicingSelectorRegistry::instance()
        .register_selector(std::make_unique<SmoothVoicingSelector>());

    // Voicing profile selectors (Phase 5)
    VoicingProfileSelectorRegistry::instance()
        .register_factory("static", []() {
          return std::unique_ptr<VoicingProfileSelector>(
              new StaticVoicingProfileSelector());
        });

    // Passage strategies
    reg.register_passage(std::make_unique<AlternatingFigureStrategy>());
    reg.register_passage(std::make_unique<PeriodPassageStrategy>());

    // Realization strategies (Compose-tier chord-event expansion)
    auto& realReg = RealizationStrategyRegistry::instance();
    realReg.register_strategy(std::make_unique<BlockRealizationStrategy>());
    realReg.register_strategy(std::make_unique<RhythmPatternRealizationStrategy>());
  }

  // --- Top-level composition ---
  void compose(Piece& piece, const PieceTemplate& tmpl) {
    setup_piece_(piece, tmpl);
    for (const auto& partTmpl : tmpl.parts) {
      if (partTmpl.role == PartRole::Harmony) continue;
      for (auto& sec : piece.sections) {
        compose_passage_(piece, tmpl, partTmpl, sec.name);
      }
    }
    realize_chord_parts_(piece, tmpl);
    realize_event_sequences_(piece, tmpl);
  }

  // --- Dispatchers called from strategies ---
  MelodicFigure compose_figure(Locus locus, const FigureTemplate& figTmpl) {
    FigureStrategy* s = StrategyRegistry::instance().resolve_figure("default_figure");
    return s->compose_figure(locus, figTmpl);
  }

  Phrase compose_phrase(Locus locus, const PhraseTemplate& phraseTmpl) {
    std::string n = phraseTmpl.strategy.empty() ? std::string("default_phrase") : phraseTmpl.strategy;
    PhraseStrategy* s = StrategyRegistry::instance().resolve_phrase(n);
    if (!s) {
      std::cerr << "Unknown phrase strategy '" << n << "', falling back to default_phrase\n";
      s = StrategyRegistry::instance().resolve_phrase("default_phrase");
    }
    return s->compose_phrase(locus, phraseTmpl);
  }

  Passage compose_passage(Locus locus, const PassageTemplate& passTmpl) {
    std::string n = passTmpl.strategy.empty() ? std::string("default_passage") : passTmpl.strategy;
    PassageStrategy* s = StrategyRegistry::instance().resolve_passage(n);
    if (!s) {
      std::cerr << "Unknown passage strategy '" << n << "', falling back to default_passage\n";
      s = StrategyRegistry::instance().resolve_passage("default_passage");
    }
    // Plan phase — strategy refines the template (may mutate motif pool).
    PassageTemplate planned = s->plan_passage(locus, passTmpl);
    // Persist planned template to PieceTemplate for regeneration / lock semantics.
    if (locus.sectionIdx >= 0 && locus.sectionIdx < (int)locus.piece->sections.size()
        && locus.partIdx >= 0 && locus.partIdx < (int)locus.pieceTemplate->parts.size()) {
      const auto& sectionName = locus.piece->sections[locus.sectionIdx].name;
      locus.pieceTemplate->parts[locus.partIdx].passages[sectionName] = planned;
    }
    // Compose phase — self-contained realization from the planned template.
    return s->compose_passage(locus, planned);
  }

  // Motif accessors moved to PieceTemplate (Task 1). Callers reach them
  // via locus.pieceTemplate->realized_motifs() / find_rhythm_motif() /
  // find_contour_motif().

  // --- Public registry lookup for Phase 2 shape dispatch ---
  // Named awkwardly to signal it exists specifically for the shape-dispatch
  // path in DefaultFigureStrategy::compose_figure. May be renamed later.
  FigureStrategy* registry_get_for_phase2(const std::string& name) const {
    return StrategyRegistry::instance().resolve_figure(name);
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
        section.chordProgression = ChordProgressionBuilder::build(sd.progressionName, sd.beats);
      }
      section.keyContexts = sd.keyContexts;

      // Populate HarmonyTimeline from whatever source provided the progression.
      if (section.chordProgression) {
        section.harmonyTimeline.set_segment(
            0.0f, sd.beats, *section.chordProgression, "authored");
      }
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

  // ---- Thin wrapper around the free realize_motifs function. -------------
  //
  // const_cast is transitional: setup_piece_ / compose() currently take
  // `const PieceTemplate&`; the free function mutates the realized maps.
  // When compose() signature changes to take non-const (Plan B's plan_*
  // requires it), this const_cast goes away.
  void realize_motifs_(const Piece& /*piece*/, const PieceTemplate& tmpl) {
    ::mforce::realize_motifs(const_cast<PieceTemplate&>(tmpl), rng_);
  }

  // -------------------------------------------------------------------------
  // realize_event_sequences_ — Stage 5 entry point.
  //
  // For each Part, walks its Passage tree (per Section) and emits realized
  // Notes into Part.elementSequence. Stage 2's exclusive Conductor dispatch
  // picks elementSequence over the tree-walk fallback once it's non-empty.
  //
  // Stage 5 handles passages composed entirely of MelodicFigure. Passages
  // containing any ChordFigure are skipped (Conductor's tree-walk fallback
  // handles them). Stage 6 will extend coverage to ChordFigure; Stage 7 to
  // chord events.
  // -------------------------------------------------------------------------
  static float section_start_beat_(const Piece& piece, const std::string& sectionName) {
    float beat = 0.0f;
    for (const auto& s : piece.sections) {
      if (s.name == sectionName) return beat;
      beat += s.beats;
    }
    return 0.0f;
  }

  void realize_event_sequences_(Piece& piece, const PieceTemplate& /*tmpl*/) {
    for (auto& part : piece.parts) {
      // Skip Parts that already have events (chord parts via add_chord, or
      // direct-build patches).
      if (!part.elementSequence.empty()) continue;

      for (const auto& sec : piece.sections) {
        auto it = part.passages.find(sec.name);
        if (it == part.passages.end()) continue;
        const Passage& passage = it->second;

        float passageStartBeat = section_start_beat_(piece, sec.name);
        float effectiveBeats = sec.beats - sec.truncateTailBeats;
        if (effectiveBeats < 0.0f) effectiveBeats = 0.0f;

        realize_passage_to_events_(part, passage, sec, passageStartBeat,
                                    effectiveBeats);
      }
    }
  }

  void realize_passage_to_events_(Part& part, const Passage& passage,
                                  const Section& section,
                                  float passageStartBeat,
                                  float maxSectionBeats) {
    const Scale& scale = passage.scaleOverride.value_or(section.scale);

    DynamicState dynamics;
    int nextMarking = 0;
    if (!passage.dynamicMarkings.empty() && passage.dynamicMarkings[0].beat <= 0.0f) {
      dynamics = DynamicState(passage.dynamicMarkings[0].level);
      nextMarking = 1;
    }

    float currentBeat = passageStartBeat;
    for (const auto& phrase : passage.phrases) {
      if (currentBeat - passageStartBeat >= maxSectionBeats) break;
      currentBeat = realize_phrase_to_events_(part, phrase, scale, currentBeat,
                                              dynamics, passage.dynamicMarkings, nextMarking,
                                              passageStartBeat, section,
                                              maxSectionBeats);
    }
  }

  float realize_phrase_to_events_(Part& part, const Phrase& phrase,
                                  const Scale& scale,
                                  float startBeat,
                                  DynamicState& dynamics,
                                  const std::vector<DynamicMarking>& markings,
                                  int& nextMarking,
                                  float passageBeatOffset,
                                  const Section& section,
                                  float maxSectionBeats) {
    float currentBeat = startBeat;
    float currentNN = phrase.startingPitch.note_number();
    constexpr int kBaseOctave = 4;  // matches Conductor::perform()'s baseOctave

    for (int f = 0; f < phrase.figure_count(); ++f) {
      const auto& fig = *phrase.figures[f];
      bool isChordFig = (dynamic_cast<const ChordFigure*>(phrase.figures[f].get()) != nullptr);

      for (int i = 0; i < fig.note_count(); ++i) {
        const auto& u = fig.units[i];

        if (maxSectionBeats >= 0.0f &&
            (currentBeat - passageBeatOffset) >= maxSectionBeats) {
          return currentBeat;
        }

        if (isChordFig && section.chordProgression) {
          // Find active chord at this beat (section-relative).
          const auto& prog = *section.chordProgression;
          float sectionBeat = currentBeat - passageBeatOffset;
          float chordBeat = 0.0f;
          int chordIdx = 0;
          for (int ci = 0; ci < prog.count(); ++ci) {
            if (chordBeat + prog.pulses.get(ci) > sectionBeat) {
              chordIdx = ci;
              break;
            }
            chordBeat += prog.pulses.get(ci);
            chordIdx = ci;
          }
          auto resolved = prog.chords.get(chordIdx).resolve(section.scale, kBaseOctave);
          currentNN = step_chord_tone(currentNN, u.step, resolved);
        } else {
          currentNN = step_note(currentNN, u.step, scale);
        }
        float soundNN = currentNN + float(u.accidental);

        float passageBeat = currentBeat - passageBeatOffset;
        while (nextMarking < int(markings.size()) &&
               markings[nextMarking].beat <= passageBeat) {
          dynamics.set_marking(markings[nextMarking], passageBeatOffset);
          nextMarking++;
        }

        if (!u.rest) {
          float vel = dynamics.velocity_at(currentBeat);
          Note n{soundNN, vel, u.duration, u.articulation, u.ornament};
          part.elementSequence.add({currentBeat, n});
        }
        currentBeat += u.duration;
      }
    }

    return currentBeat;
  }

  void realize_chord_parts_(Piece& piece, const PieceTemplate& tmpl) {
    auto& realReg = RealizationStrategyRegistry::instance();
    RealizationStrategy* blockStrat = realReg.resolve("block");

    for (int pi = 0; pi < (int)tmpl.parts.size(); ++pi) {
      const auto& partTmpl = tmpl.parts[pi];
      if (partTmpl.role != PartRole::Harmony) continue;

      Part* part = nullptr;
      for (auto& p : piece.parts) {
        if (p.name == partTmpl.name) { part = &p; break; }
      }
      if (!part) continue;

      float beatOffset = 0.0f;
      for (int si = 0; si < (int)piece.sections.size(); ++si) {
        const auto& sec = piece.sections[si];
        if (sec.harmonyTimeline.empty()) { beatOffset += sec.beats; continue; }

        auto passIt = partTmpl.passages.find(sec.name);
        const PassageTemplate* passTmpl = (passIt != partTmpl.passages.end())
            ? &passIt->second : nullptr;
        ChordAccompanimentConfig cfg;
        if (passTmpl && passTmpl->chordConfig) cfg = *passTmpl->chordConfig;

        // Rhythm-pattern source. Without one, no chord events are emitted
        // (cfg-driven default-pattern fallback was removed at Stage 11).
        const RhythmPattern* rp = (passTmpl && passTmpl->rhythmPattern)
            ? &*passTmpl->rhythmPattern : nullptr;
        if (!rp) { beatOffset += sec.beats; continue; }

        // Optional VoicingSelector (empty selectorName = legacy path).
        VoicingSelector* selector = nullptr;
        if (passIt != partTmpl.passages.end()
            && !passIt->second.voicingSelector.empty()) {
          selector = VoicingSelectorRegistry::instance()
                         .resolve(passIt->second.voicingSelector);
          if (!selector) {
            std::cerr << "Unknown voicingSelector '"
                      << passIt->second.voicingSelector
                      << "', falling back to legacy inversion/spread path\n";
          }
        }

        // VoicingProfileSelector: produces a VoicingProfile per chord.
        // Empty name = "static" (baseline profile unchanged every chord).
        // Only built when a VoicingSelector is in play.
        std::unique_ptr<VoicingProfileSelector> profileSelector;
        if (selector && passIt != partTmpl.passages.end()) {
          std::string selName = passIt->second.voicingProfileSelector.empty()
                              ? std::string("static")
                              : passIt->second.voicingProfileSelector;
          profileSelector = VoicingProfileSelectorRegistry::instance()
                                .create(selName);
          if (!profileSelector) {
            std::cerr << "Unknown voicingProfileSelector '" << selName
                      << "', falling back to static\n";
            profileSelector = VoicingProfileSelectorRegistry::instance()
                                  .create("static");
          }
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

        for (int bar = 0; bar < totalBars; ++bar) {
          float barStart = beatOffset + bar * beatsPerBar;
          const auto& pattern = rp->pattern_for_bar(bar + 1);

          float pos = barStart;
          for (float dur : pattern) {
            if (dur < 0) {
              pos += (-dur);
              continue;
            }
            const ScaleChord* sc = sec.harmonyTimeline.chord_at(pos - beatOffset);
            if (sc) {
              Chord chord;
              if (selector) {
                float beatInBar = pos - barStart;
                float beatInPassage = pos - beatOffset;
                VoicingProfile profile = profileSelector
                    ? profileSelector->profile_for_chord(
                          chordIdx, beatInBar, beatInPassage)
                    : passIt->second.voicingProfile;
                VoicingRequest req{*sc, &sec.scale, cfg.octave, dur,
                                   prevChord, std::nullopt,
                                   profile,
                                   passIt->second.voicingDictionary};
                chord = selector->select(req);
              } else {
                chord = sc->resolve(sec.scale, cfg.octave, dur,
                                    cfg.inversion, cfg.spread);
              }
              RealizationRequest realReq{chord, pos, dur, bar + 1, nullptr};
              blockStrat->realize(realReq, part->elementSequence);
              prevChord = &part->elementSequence.elements.back().chord();
              ++chordIdx;
            }
            pos += dur;
          }
        }
        beatOffset += sec.beats;
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

    ::mforce::rng::Scope rngScope(rng_);
    Locus locus{&piece, const_cast<PieceTemplate*>(&tmpl), 0, 0};
    return this->compose_passage(locus, passTmpl);
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
      Locus locus{&piece, const_cast<PieceTemplate*>(&tmpl), sectionIdx, partIdx};
      if (section) {
        locus.harmony = &section->harmonyTimeline;
      }

      // If strategy is Melody-only and no harmony exists yet, generate via ChordWalker.
      const PieceTemplate::SectionTemplate* sd = nullptr;
      for (const auto& s : tmpl.sections) {
        if (s.name == sectionName) { sd = &s; break; }
      }
      PassageStrategy* strat = StrategyRegistry::instance().resolve_passage(
          passIt->second.strategy.empty() ? "default_passage" : passIt->second.strategy);
      if (strat && strat->scope() == StrategyScope::Melody
          && section && section->harmonyTimeline.empty()
          && sd && !sd->styleName.empty()) {
        auto style = StyleTable::load_by_name(sd->styleName);
        WalkConstraint wc;
        wc.startChord = ScaleChord{0, 0, &ChordDef::get("Major")};
        wc.totalBeats = sd->beats;
        auto prog = ChordWalker::walk(style, wc, tmpl.masterSeed + sectionIdx * 1000);
        const_cast<Section*>(section)->harmonyTimeline.set_segment(
            0.0f, sd->beats, prog, "chord_walker");
        const_cast<Section*>(section)->chordProgression = prog;
      }

      ::mforce::rng::Scope rngScope(rng_);
      passage = compose_passage(locus, passIt->second);
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
// incomplete-type errors. Same pattern as DefaultFigureStrategy::compose_figure.
// ============================================================================

namespace detail {

inline PulseSequence resolve_rhythm(const FigureTemplate& ft, Locus locus,
                                     uint32_t seed, float totalBeats, float defaultPulse) {
  if (!ft.rhythmMotifName.empty()) {
    const PulseSequence* ps = locus.pieceTemplate->find_rhythm_motif(ft.rhythmMotifName);
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
    const StepSequence* ss = locus.pieceTemplate->find_contour_motif(ft.contourMotifName);
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
  uint32_t seed = ft.seed ? ft.seed : ::mforce::rng::next();
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
    const StepSequence* ss = locus.pieceTemplate->find_contour_motif(ft.contourMotifName);
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
  uint32_t seed = ft.seed ? ft.seed : ::mforce::rng::next();
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
  uint32_t seed = ft.seed ? ft.seed : ::mforce::rng::next();
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
// Out-of-line definition of DefaultFigureStrategy::compose_figure.
//
// Lives here — BELOW the Composer class — because the body needs the full
// definition of Composer to call ctx.composer->realized_motifs(). In
// default_strategies.h, Composer is only forward-declared, so the body
// can't be inline there.
// ============================================================================

inline MelodicFigure DefaultFigureStrategy::compose_figure(
    Locus locus, const FigureTemplate& figTmpl) {
  const Scale& scale = locus.piece->sections[locus.sectionIdx].scale;
  // Use the figure's seed for reproducibility
  uint32_t figSeed = figTmpl.seed ? figTmpl.seed : ::mforce::rng::next();

  switch (figTmpl.source) {
    case FigureSource::Locked:
      if (figTmpl.lockedFigure) return *figTmpl.lockedFigure;
      return FigureBuilder(::mforce::rng::next()).single_note(1.0f);  // fallback

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
      auto it = locus.pieceTemplate->realized_motifs().find(figTmpl.motifName);
      if (it != locus.pieceTemplate->realized_motifs().end()) return it->second;
      return generate_figure(figTmpl, figSeed);
    }

    case FigureSource::Transform: {
      auto it = locus.pieceTemplate->realized_motifs().find(figTmpl.motifName);
      MelodicFigure base = (it != locus.pieceTemplate->realized_motifs().end())
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
          FigureStrategy* s = StrategyRegistry::instance().resolve_figure(shapeName);
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
// Out-of-line definition of DefaultPassageStrategy::compose_passage.
//
// Lives here — BELOW the Composer class — because the body calls
// ctx.composer->compose_phrase(...) and Composer is forward-declared only in
// default_strategies.h. Same pattern as DefaultFigureStrategy::compose_figure.
// ============================================================================

inline Passage DefaultPassageStrategy::compose_passage(
    Locus locus, const PassageTemplate& passTmpl) {
  Passage passage;

  if (!passTmpl.startingPitch) {
    // Should not happen — loader refuses templates without startingPitch.
    return passage;
  }

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
    if (!localTmpl.startingPitch) {
      // Compute cursor from the phrases realized so far in our local passage.
      Pitch cursor = *passTmpl.startingPitch;
      const Scale& scale = locus.piece->sections[locus.sectionIdx].scale;
      PitchReader reader(scale);
      reader.set_pitch(cursor);
      for (auto& ph : passage.phrases) {
        for (auto& fig : ph.figures) {
          for (auto& u : fig->units) {
            reader.step(u.step);
          }
        }
      }
      localTmpl.startingPitch = reader.get_pitch();
    }

    Locus phraseLocus = locus.with_phrase(i);

    std::string pn = phraseTmpl.strategy.empty() ? std::string("default_phrase") : phraseTmpl.strategy;
    PhraseStrategy* ps = StrategyRegistry::instance().resolve_phrase(pn);
    if (!ps) {
      std::cerr << "Unknown phrase strategy '" << pn << "', falling back to default_phrase\n";
      ps = StrategyRegistry::instance().resolve_phrase("default_phrase");
    }
    Phrase phrase = ps->compose_phrase(phraseLocus, localTmpl);

    passage.add_phrase(std::move(phrase));
  }

  return passage;
}

// ============================================================================
// Out-of-line definition of DefaultPhraseStrategy::compose_phrase.
//
// Lives here — BELOW the Composer class — because the body calls
// ctx.composer->compose_figure(...) and Composer is forward-declared only in
// default_strategies.h. Same pattern as the other two out-of-line definitions.
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

    // MelodicFunction-driven shape selection — same logic as
    // classical_composer.h:197-202.
    FigureTemplate figTmpl = phraseTmpl.figures[i];
    if (phraseTmpl.function != MelodicFunction::Free
        && figTmpl.source == FigureSource::Generate
        && figTmpl.shape == FigureShape::Free) {
      figTmpl.shape = DefaultFigureStrategy::choose_shape(
          phraseTmpl.function, i, numFigs, ::mforce::rng::next());
    }

    // Dispatch to the figure level. DefaultFigureStrategy's Literal path
    // reads the cursor via pitch_before(locus), which walks the phrase/
    // passage structure so far. Since the phrase we're building hasn't been
    // inserted into the piece yet, pitch_before sees an empty passage and
    // falls back to the passage template startingPitch — NOT the right
    // answer for figures mid-phrase. Work around by stamping the cursor
    // into the figure template via a synthetic startingPitch-bearing
    // FigureTemplate is not supported; instead, we rely on the fact that
    // the pre-refactor behavior used ctx.cursor advanced by prior figure
    // net_steps to drive the Literal path's absoluteDeg(cursor). The
    // runningReader here tracks that, and for the Literal path we need to
    // provide it. Simplest fix: inline the Literal path's cursor-relative
    // computation directly here for Literal figures; or extend pitch_before
    // to accept an override. For Task 6 minimum viable: if the figure is
    // Literal, copy its pitches through a small helper that honors the
    // runningReader. For non-Literal figures, the cursor isn't read.
    Locus figLocus = locus.with_figure(i);
    FigureStrategy* fs = StrategyRegistry::instance().resolve_figure("default_figure");
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

    // Apply FigureConnector.leadStep to the new figure's first unit.
    // Applies for ALL figures including i=0 — connectors[0].leadStep places
    // the first figure relative to the phrase's startingPitch. For
    // placement-neutral motifs (first step = 0), this is the sole placement
    // mechanism. The elide/adjust block above keeps its i>0 guard because
    // those operate on the PREVIOUS figure (which doesn't exist for i=0).
    if (i < int(phraseTmpl.connectors.size())
        && phraseTmpl.connectors[i] && !fig.units.empty()) {
      fig.units[0].step += phraseTmpl.connectors[i]->leadStep;
    }

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
      apply_cadence(phrase, phraseTmpl, scale);
    }
  }

  return phrase;
}

// ---------------------------------------------------------------------------
// Out-of-line definition of AlternatingFigureStrategy::compose_passage.
//
// Placed here (after Composer is fully defined) to break the circular
// dependency: AlternatingFigureStrategy calls ctx.composer->compose_figure,
// but Composer is only forward-declared in strategy.h.
// Same pattern as DefaultPassageStrategy/DefaultPhraseStrategy/DefaultFigureStrategy.
// ---------------------------------------------------------------------------
inline Passage AlternatingFigureStrategy::compose_passage(
    Locus locus, const PassageTemplate& pt) {

  const Scale& scale = locus.piece->sections[locus.sectionIdx].scale;
  const auto& sectionChordProg = locus.piece->sections[locus.sectionIdx].chordProgression;
  if (!sectionChordProg || sectionChordProg->count() == 0) {
    throw std::runtime_error("AFS: no chord progression on section");
  }
  if (pt.phrases.empty() || pt.phrases[0].figures.size() < 2) {
    throw std::runtime_error(
        "AFS: need 1 phrase with at least 2 figure templates (A and B)");
  }

  const auto& chordProg = *sectionChordProg;
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

  FigureStrategy* fs = StrategyRegistry::instance().resolve_figure("default_figure");

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
      float curNN = runningReader.get_note_number();
      auto resolved = chordProg.chords.get(ci).resolve(scale, runningReader.get_octave());
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

} // namespace mforce
