#pragma once
// The two functions the default figure and phrase strategies both call
// (and the strategies built on them).
#include "mforce/compose/templates.h"
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/core/randomizer.h"
#include <cmath>
#include <cstdint>

namespace mforce {

// The 0-based degree of `pitch`'s pitch class in `scale`; 0 when it is not
// a degree of the scale.
inline int degree_in_scale(const Pitch& pitch, const Scale& scale) {
    float rel = std::fmod(float(pitch.pitchDef->offset - scale.offset()) + 12.0f, 12.0f);
    float accum = 0;
    for (int d = 0; d < scale.length(); ++d) {
      if (std::abs(accum - rel) < 0.5f) return d;
      accum += scale.ascending_step(d);
    }
    return 0; // fallback to root
}

// The shape a Generate figure takes at `posInPhrase` of `totalFigures` in a
// phrase of function `func`, drawn from `seed`. approachArrival: comp #9
// knob, from PhraseTemplate::cadentialArrival; false (default) keeps the
// standing HeldNote arrival.
inline FigureShape choose_shape(
    MelodicFunction func, int posInPhrase, int totalFigures, uint32_t seed,
    bool approachArrival = false) {
    Randomizer r(seed);
    bool isLast = (posInPhrase == totalFigures - 1);
    bool isFirst = (posInPhrase == 0);

    switch (func) {
      case MelodicFunction::Statement: {
        // Opening motif: clear, memorable shapes
        static const FigureShape opts[] = {
          FigureShape::RepeatedNote, FigureShape::TriadicOutline,
          FigureShape::ScalarRun, FigureShape::NeighborTone,
          FigureShape::Fanfare
        };
        if (isLast) return FigureShape::HeldNote;
        return opts[r.int_range(0, 4)];
      }

      case MelodicFunction::Development: {
        // Extension/variation: more complex shapes
        static const FigureShape opts[] = {
          FigureShape::Zigzag, FigureShape::ScalarReturn,
          FigureShape::LeapAndFill, FigureShape::Cambiata,
          FigureShape::ScalarRun
        };
        if (isLast) return FigureShape::ScalarRun;
        return opts[r.int_range(0, 4)];
      }

      case MelodicFunction::Transition: {
        static const FigureShape opts[] = {
          FigureShape::ScalarRun, FigureShape::LeapAndFill,
          FigureShape::Zigzag, FigureShape::ScalarReturn
        };
        return opts[r.int_range(0, 3)];
      }

      case MelodicFunction::Cadential: {
        if (isFirst || !isLast)
          return FigureShape::CadentialApproach;
        // comp #9. HeldNote leaves apply_cadence a single unit, so
        // build_approach_steps takes its far branch and leaps onto the
        // target — sustained, but unapproached. "approach" hands it a real
        // figure to reshape instead. Default unchanged; see
        // PhraseTemplate::cadentialArrival.
        return approachArrival ? FigureShape::CadentialApproach
                               : FigureShape::HeldNote;
      }

      case MelodicFunction::Free:
      default:
        return FigureShape::Free;
    }
}

} // namespace mforce
