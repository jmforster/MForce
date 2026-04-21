#pragma once
#include "mforce/music/voicing_selector.h"
#include "mforce/music/basics.h"
#include <algorithm>
#include <cmath>
#include <iostream>
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

    // Rule-native search space:
    //   inversion ∈ [0, N-1]   — list rotation (bass = Kth chord tone)
    //   spread    ∈ [0, N-1]   — voicing-gap walk rule
    //   rootOctave search ∈ [req.rootOctave - 1, +1]
    // Allow-lists on the profile filter (inversion, spread) before scoring.
    const int kOctRange = 1;
    cands.reserve(n * n * (2 * kOctRange + 1));

    const auto& inversionAllow = req.profile.allowedInversions;
    const auto& spreadAllow    = req.profile.allowedSpreads;
    auto invAllowed = [&](int v) {
      return inversionAllow.empty() ||
             std::find(inversionAllow.begin(), inversionAllow.end(), v)
               != inversionAllow.end();
    };
    auto sprAllowed = [&](int v) {
      return spreadAllow.empty() ||
             std::find(spreadAllow.begin(), spreadAllow.end(), v)
               != spreadAllow.end();
    };

    for (int inv = 0; inv < n; ++inv) {
      if (!invAllowed(inv)) continue;
      for (int spr = 0; spr < n; ++spr) {
        if (!sprAllowed(spr)) continue;
        for (int dOct = -kOctRange; dOct <= kOctRange; ++dOct) {
          Chord c = sc.resolve(*req.scale, req.rootOctave + dOct,
                               req.durationBeats, inv, spr);
          Candidate cand;
          cand.chord = std::move(c);
          cand.vl = voice_leading_distance(*req.previous, cand.chord);
          cand.common = common_tones(*req.previous, cand.chord);
          cand.melody = req.melodyPitch
                      ? melody_penalty(cand.chord, *req.melodyPitch)
                      : 0.0f;
          cands.push_back(std::move(cand));
        }
      }
    }

    // Pathological config: allow-lists eliminated every candidate.
    // Fall back to a guaranteed-valid (inv=0, spread=0, rootOctave=req.rootOctave).
    if (cands.empty()) {
      static bool warned = false;
      if (!warned) {
        std::cerr << "SmoothVoicingSelector: allow-lists eliminated all "
                     "candidates; using inv=0 spread=0 fallback\n";
        warned = true;
      }
      return sc.resolve(*req.scale, req.rootOctave,
                        req.durationBeats, 0, 0);
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

    float p = std::clamp(req.profile.priority, 0.0f, 1.0f);
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
