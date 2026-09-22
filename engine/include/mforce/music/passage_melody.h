#pragma once
// Passage-string -> template melody derivation (comp crawl, spec
// docs/superpowers/specs/2026-09-21-comp-crawl-mary-design.md §4).
// A comp-purpose passage string ('|' = structural phrase boundary) becomes
// PhraseTemplates of Locked figures: step[0]=0 inside a figure, the bridge
// between figures rides FigureConnector.leadStep.
#include "mforce/music/basics.h"
#include "mforce/music/parse_util.h"
#include "mforce/music/templates.h"
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mforce {

// Diatonic grid index of a note number: octave * scale-length + degree,
// measured from the scale's tonic pitch class. Equal note numbers always
// map to equal indices, so deltas are scale-step distances. Throws on a
// non-scale tone — crawl material is diatonic by construction, and a
// silent snap would hide a transcription error.
inline int scale_grid_index(float nn, const Scale& scale) {
    const int len = scale.length();
    const int total = int(nn) - scale.offset();
    const int oct = (total >= 0) ? total / 12 : -((-total + 11) / 12);
    const int rem = total - 12 * oct;   // 0..11 semitones above a tonic
    float acc = 0.0f;
    for (int d = 0; d < len; ++d) {
        if (int(acc + 0.5f) == rem) return oct * len + d;
        acc += scale.ascending_step(d);
    }
    throw std::runtime_error(
        "passage melody: note number " + std::to_string(int(nn)) +
        " is not a tone of the scale");
}

inline int scale_steps_between(float nnFrom, float nnTo, const Scale& scale) {
    return scale_grid_index(nnTo, scale) - scale_grid_index(nnFrom, scale);
}

} // namespace mforce
