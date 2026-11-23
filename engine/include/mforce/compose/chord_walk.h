#pragma once
#include "mforce/music/figures.h"
#include "mforce/music/pitch_reader.h"
#include "mforce/music/pitch_walker.h"

namespace mforce {

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

} // namespace mforce
