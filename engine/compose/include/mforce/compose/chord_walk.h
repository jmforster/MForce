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
// chord's resolved tones. The walk is a random walk in register, so it must
// walk exactly what realization walks, or the cursor a strategy computes a
// cadential approach from is not the pitch the listener hears (and the
// approach lands on the wrong degree), and a descending bias can run the
// cursor below note number 0, where Pitch::from_note_number does `n % 12` on
// a negative and hands PitchDef::get an offset that does not exist
// ("Unknown PitchDef offset: -3"). So, as realization does, it:
//
//   * applies EVERY unit's step in turn, not the figure's NET step in one
//     step_chord_tone-style hop (pitch_walker.h: step_chord_tone is not
//     linear over walks, so one hop of n is not n hops of one);
//   * resolves the chord at kBaseOctave, not the RUNNING octave, which would
//     make the candidate window follow the cursor, so a descending bias
//     compounds instead of being re-anchored.
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
