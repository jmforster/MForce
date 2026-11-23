#include "mforce/music/realize.h"
#include "mforce/music/dynamic_state.h"
#include "mforce/music/pitch_walker.h"
#include <string>
#include <vector>

namespace mforce {

namespace {

float section_start_by_name(const Piece& piece, const std::string& sectionName) {
  for (std::size_t k = 0; k < piece.sections.size(); ++k)
    if (piece.sections[k].name == sectionName) return section_start_beat(piece.sections, k);
  return 0.0f;
}

// One phrase's notes from `startBeat`; returns the beat after its last unit.
// Stops at the section's played length (maxSectionBeats, relative to
// passageBeatOffset): no note starts at or past it, and a note that would
// cross it is cut to end on it.
float realize_phrase(Part& part, const Phrase& phrase,
                     const Scale& baseScale,
                     float startBeat,
                     DynamicState& dynamics,
                     const std::vector<DynamicMarking>& markings,
                     int& nextMarking,
                     float passageBeatOffset,
                     const Section& section,
                     float maxSectionBeats,
                     bool keyAware) {
  float currentBeat = startBeat;
  float currentNN = phrase.startingPitch.note_number();
  constexpr int kBaseOctave = 4;  // matches Conductor::perform()'s baseOctave

  // Key-aware realization: `scale` tracks the section's key timeline; when
  // it changes, the running cursor is SNAPPED into the new scale before the
  // step is applied — otherwise a step of 0 across the boundary would carry
  // the old key's pitch (an F held through a change into G major).
  // keyAware is false whenever the passage pins a scaleOverride or the
  // section has no keyContexts.
  Scale scale = baseScale;

  for (int f = 0; f < phrase.figure_count(); ++f) {
    const auto& fig = *phrase.figures[f];
    bool isChordFig = (dynamic_cast<const ChordFigure*>(phrase.figures[f].get()) != nullptr);

    for (int i = 0; i < fig.note_count(); ++i) {
      const auto& u = fig.units[i];

      if (maxSectionBeats >= 0.0f &&
          (currentBeat - passageBeatOffset) >= maxSectionBeats) {
        return currentBeat;
      }

      if (keyAware) {
        Scale active = section.active_scale_at(currentBeat - passageBeatOffset);
        if (active.pitchDef != scale.pitchDef
            || active.scaleDef != scale.scaleDef) {
          currentNN = snap_to_scale(currentNN, active);
          scale = active;
        }
      }

      // A chord figure walks the tones of the chord sounding at this beat
      // (section-relative); a section without harmony walks the scale.
      const ScaleChord* chord = isChordFig
          ? section.harmonyTimeline.chord_at_or_last(currentBeat - passageBeatOffset)
          : nullptr;
      if (chord) {
        auto resolved = chord->resolve(section.scale, kBaseOctave);
        // For unit 0 of the figure, fold FC.leadStep into the chord-tone
        // walk: a single step_chord_tone(currentNN, leadStep + u.step, ...)
        // call snaps once and walks once. Splitting into two calls would
        // double-snap and drift. step_chord_tone is NOT linear over walks.
        int stepFromLead = (i == 0 && f < int(phrase.connectors.size()))
            ? phrase.connectors[f].leadStep : 0;
        currentNN = step_chord_tone(currentNN, stepFromLead + u.step, resolved);
      } else {
        int stepFromLead = (i == 0 && f < int(phrase.connectors.size()))
            ? phrase.connectors[f].leadStep : 0;
        currentNN = step_note(currentNN, stepFromLead + u.step, scale);
      }
      float soundNN = currentNN + float(u.accidental);

      float passageBeat = currentBeat - passageBeatOffset;
      while (nextMarking < int(markings.size()) &&
             markings[nextMarking].beat <= passageBeat) {
        dynamics.set_marking(markings[nextMarking], passageBeatOffset);
        nextMarking++;
      }

      // Clamp a note that would ring past the section's end, so a passage
      // composed longer than its section ends exactly on the cut.
      float dur = u.duration;
      if (maxSectionBeats >= 0.0f) {
        const float room = maxSectionBeats - (currentBeat - passageBeatOffset);
        if (dur > room) dur = room;
      }

      if (!u.rest && dur > 0.0f) {
        float vel = dynamics.velocity_at(currentBeat);
        Note n{soundNN, vel, dur, u.articulation, u.ornament};
        part.elementSequence.add({currentBeat, n});
      }
      currentBeat += dur;
    }
  }

  return currentBeat;
}

void realize_passage(Part& part, const Passage& passage,
                     const Section& section,
                     float passageStartBeat,
                     float maxSectionBeats) {
  // A passage-level scaleOverride pins the whole passage; without one the
  // realize step reads the section's key timeline per note (see
  // realize_phrase). Scale is two pointers, so the per-note lookup costs
  // nothing worth optimizing.
  const Scale& scale = passage.scaleOverride.value_or(section.scale);
  const bool keyAware = !passage.scaleOverride && !section.keyContexts.empty();

  DynamicState dynamics;
  int nextMarking = 0;
  if (!passage.dynamicMarkings.empty() && passage.dynamicMarkings[0].beat <= 0.0f) {
    dynamics = DynamicState(passage.dynamicMarkings[0].level);
    nextMarking = 1;
  }

  float currentBeat = passageStartBeat;
  for (const auto& phrase : passage.phrases) {
    if (currentBeat - passageStartBeat >= maxSectionBeats) break;
    currentBeat = realize_phrase(part, phrase, scale, currentBeat,
                                 dynamics, passage.dynamicMarkings, nextMarking,
                                 passageStartBeat, section,
                                 maxSectionBeats, keyAware);
  }
}

} // namespace

void realize_events(Piece& piece) {
  for (auto& part : piece.parts) {
    // Skip Parts that already have events (chord parts via add_chord, or
    // direct-build patches).
    if (!part.elementSequence.empty()) continue;

    for (const auto& sec : piece.sections) {
      auto it = part.passages.find(sec.name);
      if (it == part.passages.end()) continue;
      const Passage& passage = it->second;

      float passageStartBeat = section_start_by_name(piece, sec.name);
      float effectiveBeats = played_beats(sec);

      realize_passage(part, passage, sec, passageStartBeat, effectiveBeats);
    }
  }
}

} // namespace mforce
