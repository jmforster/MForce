#pragma once
#include "mforce/music/voicing_selector.h"
#include "mforce/music/basics.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace mforce {

// ---------------------------------------------------------------------------
// SmoothVoicingSelector — picks the inversion of each chord that minimizes
// voice-leading distance from the previous chord on the same Part.
//
// Algorithm:
//   - If no previous chord, return the ScaleChord resolved at inversion=0.
//   - Otherwise enumerate inversions [0..N-1] for N pitches, score each by
//     greedy symmetric nearest-tone distance from the previous chord, and
//     return the lowest-scoring candidate.
//
// Scoring is genre-neutral. Optional melody-pitch penalty/bonus nudges the
// top voice toward the melody note when provided.
// ---------------------------------------------------------------------------
class SmoothVoicingSelector : public VoicingSelector {
 public:
  std::string name() const override { return "smooth"; }

  Chord select(const VoicingRequest& req) override {
    if (!req.scale) {
      // No scale context — fall back to trivial resolve.
      return req.scaleChord.resolve(Scale{}, req.rootOctave, req.durationBeats, 0, 0);
    }

    Chord base = req.scaleChord.resolve(*req.scale, req.rootOctave,
                                        req.durationBeats, 0, 0);
    if (!req.previous || base.pitches.empty()) return base;

    int nInversions = int(base.pitches.size());
    Chord best = base;
    float bestScore = std::numeric_limits<float>::infinity();

    for (int inv = 0; inv < nInversions; ++inv) {
      Chord candidate = req.scaleChord.resolve(*req.scale, req.rootOctave,
                                               req.durationBeats, inv, 0);
      float score = voice_leading_distance(*req.previous, candidate);
      if (req.melodyPitch) {
        score += melody_penalty(candidate, *req.melodyPitch);
      }
      if (score < bestScore) {
        bestScore = score;
        best = candidate;
      }
    }
    return best;
  }

 private:
  // Greedy symmetric nearest-tone distance. For every pitch in prev, find the
  // closest pitch in cand; sum the absolute semitone distances. Then repeat
  // cand -> prev. Sum of both directions is the score. Lower = smoother.
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

  // Penalty if the top voice clashes with the melody (within a half-step but
  // not equal). Bonus if the chord contains the melody pitch at all.
  static float melody_penalty(const Chord& cand, const Pitch& melody) {
    if (cand.pitches.empty()) return 0.0f;
    float mnn = melody.note_number();

    bool contains = false;
    for (const auto& c : cand.pitches) {
      float d = std::abs(c.note_number() - mnn);
      if (d < 0.01f) { contains = true; break; }
    }

    float topNn = cand.pitches.back().note_number();
    float topDist = std::abs(topNn - mnn);
    float penalty = 0.0f;
    if (topDist > 0.01f && topDist < 1.01f) penalty += 4.0f;  // half-step clash
    if (!contains) penalty += 2.0f;                            // melody not in chord
    else penalty -= 2.0f;                                      // bonus if contained
    return penalty;
  }
};

}  // namespace mforce
