#include "mforce/compose/compose.h"
#include "mforce/compose/chord_progression_builder.h"
#include "mforce/compose/chord_walker.h"
#include "mforce/compose/default_figure_strategy.h"
#include "mforce/compose/default_passage_strategy.h"
#include "mforce/compose/default_phrase_strategy.h"
#include "mforce/compose/grid_complete.h"
#include "mforce/compose/realization_strategy.h"
#include "mforce/compose/style_table.h"
#include "mforce/compose/templates.h"
#include "mforce/music/harmony_timeline.h"
#include "mforce/music/pitch_reader.h"
#include "mforce/music/realize.h"
#include "mforce/music/structure.h"
#include "mforce/voicing/voicing_profile_selector.h"
#include "mforce/voicing/voicing_selector.h"
#include "mforce/core/randomizer.h"
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace mforce {

// ---------------------------------------------------------------------------
// realize_motifs — free function
//
// Walks a PieceTemplate's Motif declarations and populates its realizedMotifs
// / realizedRhythms / realizedContours maps. Composer::setup_piece_ calls it
// on the plan. Non-const PieceTemplate& because it writes into the realized
// maps. Takes a Randomizer& for generation-seed fallback draws.
// ---------------------------------------------------------------------------
void realize_motifs(PieceTemplate& tmpl, Randomizer& rng) {
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

namespace {

// ---------------------------------------------------------------------------
// Composer
//
// Walks a plan (the working copy of a request) and dispatches to the kit's
// strategies at each level of the hierarchy. Every strategy draws from the
// one Randomizer it owns, reached through Locus::rng. compose(), below,
// is the entry a host calls (compose.h).
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
    PassageStrategy* s = &locus.kit->strategies.passage(passTmpl.strategy.name);
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

  // How one harmony part's chords are voiced in one section: the passage's
  // hints and selectors, and the chord voiced before (by value, at the
  // length it was emitted at), which a voicing selector smooths away from.
  struct ChordVoicing {
    const PassageTemplate* passTmpl{nullptr};
    ChordAccompanimentConfig cfg;
    VoicingSelector* selector{nullptr};
    std::unique_ptr<VoicingProfileSelector> profileSelector;
    float sectionStart{0.0f};
    float beatsPerBar{4.0f};
    std::optional<Chord> previous;
    int chordIdx{0};
  };

  void realize_chord_parts_(Piece& piece, const PieceTemplate& tmpl) {
    for (int pi = 0; pi < (int)tmpl.parts.size(); ++pi) {
      const auto& partTmpl = tmpl.parts[pi];
      if (partTmpl.role != PartRole::Harmony) continue;

      Part* part = nullptr;
      for (auto& p : piece.parts) {
        if (p.name == partTmpl.name) { part = &p; break; }
      }
      if (!part) continue;

      for (int si = 0; si < (int)piece.sections.size(); ++si) {
        realize_chord_section_(*part, partTmpl, pi, piece, si, tmpl.masterSeed);
      }
    }
  }

  // One harmony part's events for one section: the harmony chosen, the
  // voicing set up, and the passage's realization strategy run over it.
  void realize_chord_section_(Part& part, const PartTemplate& partTmpl, int pi,
                              const Piece& piece, int si, uint32_t masterSeed) {
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
    if (timeline->empty()) return;

    ChordVoicing v = plan_chord_voicing_(passTmpl, cfg, sec, beatOffset,
        masterSeed ^ 0x566F6953u ^ (uint32_t(si) * 100u + uint32_t(pi)));

    // The realization: empty name = "block", one event per authored chord at
    // the length the progression gives it; "rhythm_pattern" strikes by the
    // bar and samples the harmony at each strike.
    const StrategyChoice noChoice;
    const StrategyChoice& choice = passTmpl ? passTmpl->realization : noChoice;
    RealizationStrategy& strategy = kit_.strategies.realization(choice.name);
    RealizationContext ctx{*timeline, sec, beatOffset, choice,
        [&](const ScaleChord& sc, float pos, float dur) {
          return voice_chord_(v, sec, sc, pos, dur);
        }};
    strategy.realize(ctx, part.elementSequence);
  }

  // The voicing selector and profile selector a passage names, the latter
  // reset to its seed. Neither is built when the passage names no voicing
  // selector.
  ChordVoicing plan_chord_voicing_(const PassageTemplate* passTmpl,
                                   const ChordAccompanimentConfig& cfg,
                                   const Section& sec, float sectionStart,
                                   uint32_t seed) {
    ChordVoicing v;
    v.passTmpl = passTmpl;
    v.cfg = cfg;
    v.sectionStart = sectionStart;

    // Optional VoicingSelector (empty name = inversion/spread path).
    if (passTmpl && !passTmpl->voicingSelector.empty()) {
      v.selector = &kit_.voicings.selector(passTmpl->voicingSelector);
    }

    // VoicingProfileSelector: produces a VoicingProfile per chord.
    // Empty name = "static" (baseline profile unchanged every chord).
    // Only built when a VoicingSelector is in play.
    if (v.selector && passTmpl) {
      v.profileSelector = kit_.voicings.make_profile_selector(
          passTmpl->voicingProfileSelector.name, passTmpl->voicingProfile,
          passTmpl->voicingProfileSelector.settings);
      v.profileSelector->reset(seed);
    }

    v.beatsPerBar = float(sec.meter.beats_per_bar());
    return v;
  }

  // One chord symbol voiced at piece beat `pos` for `dur` beats. The voiced
  // chord, at `dur`, becomes the previous chord of the next call.
  Chord voice_chord_(ChordVoicing& v, const Section& sec, const ScaleChord& sc,
                     float pos, float dur) {
    const ChordAccompanimentConfig& cfg = v.cfg;
    float beatInPassage = pos - v.sectionStart;
    int bar = int(beatInPassage / v.beatsPerBar);
    // Chords follow the section's key contexts, as melody realization does.
    // Without them active_scale_at returns the section scale; with them, one
    // authored progression walks through keys (Matt's Bruckner pedal).
    Scale activeScale = sec.active_scale_at(beatInPassage);
    Chord chord;
    if (sc.pin) {
      // An authored VoicingPin outranks any selector: this chord's
      // voicing is part of its identity (cadential role), not a
      // preference for the motion-minimizer to trade away. The pinned
      // chord still becomes the previous chord, so the NEXT chord's
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
    } else if (v.selector) {
      float beatInBar = beatInPassage - bar * v.beatsPerBar;
      VoicingProfile profile = v.profileSelector
          ? v.profileSelector->profile_for_chord(
                v.chordIdx, beatInBar, beatInPassage)
          : v.passTmpl->voicingProfile;
      VoicingRequest req{sc, &activeScale, cfg.octave, dur,
                         v.previous ? &*v.previous : nullptr, std::nullopt,
                         profile,
                         v.passTmpl->voicingDictionary};
      chord = v.selector->select(req);
    } else {
      chord = sc.resolve(activeScale, cfg.octave, dur,
                         cfg.inversion, cfg.spread);
    }
    Chord emitted = chord;
    emitted.dur = dur;
    v.previous = emitted;
    ++v.chordIdx;
    return chord;
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
      PassageStrategy* strat = &kit_.strategies.passage(passIt->second.strategy.name);
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

} // namespace

Composition compose(const PieceTemplate& request, const ComposeKit& kit) {
  Composition result;
  result.plan = request;
  Composer composer(request.masterSeed, kit);
  composer.compose(result.piece, result.plan);
  return result;
}

} // namespace mforce
