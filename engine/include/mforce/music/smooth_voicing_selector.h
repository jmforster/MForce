#pragma once
#include "mforce/music/voicing_selector.h"
#include "mforce/music/basics.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// SmoothVoicingSelector — picks the inversion of each chord that minimizes a
// composite of voice-leading distance and common-tone preference from the
// previous chord on the same Part.
//
// The 'priority' field on VoicingRequest blends the two metrics:
//   priority = 0.0 → pure voice-leading distance (minimize total motion)
//   priority = 1.0 → pure common-tone preference (maximize stationary voices)
//   priority = 0.5 → equally weighted composite
// Each metric is min-max normalized across the candidate inversions so the
// weights are genuinely comparable regardless of absolute scale.
//
// Optional melody-pitch penalty/bonus (stubbed) nudges the top voice toward
// the melody note when provided.
// ---------------------------------------------------------------------------
class SmoothVoicingSelector : public VoicingSelector {
 public:
  std::string name() const override { return "smooth"; }

  Chord select(const VoicingRequest& req) override {
    if (!req.scale) {
      return req.scaleChord.resolve(Scale{}, req.rootOctave, req.durationBeats, 0, 0);
    }

    // Optionally override the chord-quality ChordDef from a named dictionary
    // (e.g. "Piano" gives wider comping voicings). Falls back to the quality
    // pointer already on the ScaleChord (canonic) if lookup fails.
    ScaleChord sc = req.scaleChord;
    if (!req.dictionaryName.empty() && sc.quality) {
      try {
        const auto& dict = ChordDictionary::get(req.dictionaryName);
        sc.quality = &dict.get_chord_def(sc.quality->shortName.empty()
                                         ? std::string("M")
                                         : sc.quality->shortName);
      } catch (...) {
        // leave sc.quality as-is
      }
    }

    Chord base = sc.resolve(*req.scale, req.rootOctave,
                            req.durationBeats, 0, 0);
    if (!req.previous || base.pitches.empty()) return base;

    int n = int(base.pitches.size());

    struct Candidate {
      Chord chord;
      float vl;
      int common;
      float melody;
    };
    std::vector<Candidate> cands;
    cands.reserve(2 * n - 1);

    // Enumerate inversions from -(n-1) through +(n-1). Positive = move
    // lowest K pitches up an octave (classical "Kth inversion"). Negative
    // = move highest |K| pitches down an octave (puts a higher-degree
    // voice in the bass at lower register — e.g., canonical 3rd inversion
    // of Em7 with D in the bass via -1).
    for (int inv = -(n - 1); inv < n; ++inv) {
      Chord c = sc.resolve(*req.scale, req.rootOctave,
                           req.durationBeats, inv, 0);
      Candidate cand;
      cand.chord = std::move(c);
      cand.vl = voice_leading_distance(*req.previous, cand.chord);
      cand.common = common_tones(*req.previous, cand.chord);
      cand.melody = req.melodyPitch ? melody_penalty(cand.chord, *req.melodyPitch) : 0.0f;
      cands.push_back(std::move(cand));
    }

    // Min-max normalize each metric across the candidate pool.
    float vlMin = std::numeric_limits<float>::infinity();
    float vlMax = -std::numeric_limits<float>::infinity();
    int ctMin = std::numeric_limits<int>::max();
    int ctMax = std::numeric_limits<int>::min();
    for (const auto& c : cands) {
      vlMin = std::min(vlMin, c.vl); vlMax = std::max(vlMax, c.vl);
      ctMin = std::min(ctMin, c.common); ctMax = std::max(ctMax, c.common);
    }

    float p = std::clamp(req.priority, 0.0f, 1.0f);
    constexpr float kTiebreakVL = 0.01f;  // always give VL a sliver of weight so ties
                                          // resolve toward smoother voicings
    float bestScore = std::numeric_limits<float>::infinity();
    const Candidate* best = &cands[0];
    for (const auto& c : cands) {
      float vlNorm = (vlMax > vlMin) ? (c.vl - vlMin) / (vlMax - vlMin) : 0.0f;
      // common-tone normalization: higher raw = better, so invert to a penalty
      float ctNorm = (ctMax > ctMin)
                   ? 1.0f - float(c.common - ctMin) / float(ctMax - ctMin)
                   : 0.0f;
      float score = (1.0f - p) * vlNorm
                  + p * ctNorm
                  + kTiebreakVL * vlNorm
                  + c.melody;
      if (score < bestScore) {
        bestScore = score;
        best = &c;
      }
    }
    return best->chord;
  }

 private:
  static float voice_leading_distance(const Chord& prev, const Chord& cand) {
    if (prev.pitches.empty() || cand.pitches.empty()) return 0.0f;
    float total = 0.0f;
    for (const auto& p : prev.pitches) {
      float pn = p.note_number();
      float nearest = std::numeric_limits<float>::infinity();
      for (const auto& c : cand.pitches) {
        float d = std::abs(c.note_number() - pn);
        if (d < nearest) nearest = d;
      }
      total += nearest;
    }
    for (const auto& c : cand.pitches) {
      float cn = c.note_number();
      float nearest = std::numeric_limits<float>::infinity();
      for (const auto& p : prev.pitches) {
        float d = std::abs(p.note_number() - cn);
        if (d < nearest) nearest = d;
      }
      total += nearest;
    }
    return total;
  }

  // Count voices in prev whose exact pitch (within tolerance) appears in cand.
  static int common_tones(const Chord& prev, const Chord& cand) {
    int n = 0;
    for (const auto& p : prev.pitches) {
      float pn = p.note_number();
      for (const auto& c : cand.pitches) {
        if (std::abs(c.note_number() - pn) < 0.01f) { ++n; break; }
      }
    }
    return n;
  }

  static float melody_penalty(const Chord& cand, const Pitch& melody) {
    if (cand.pitches.empty()) return 0.0f;
    float mnn = melody.note_number();
    bool contains = false;
    for (const auto& c : cand.pitches) {
      if (std::abs(c.note_number() - mnn) < 0.01f) { contains = true; break; }
    }
    float topNn = cand.pitches.back().note_number();
    float topDist = std::abs(topNn - mnn);
    float penalty = 0.0f;
    if (topDist > 0.01f && topDist < 1.01f) penalty += 4.0f;
    if (!contains) penalty += 2.0f;
    else penalty -= 2.0f;
    return penalty;
  }
};

}  // namespace mforce
