#pragma once
#include <vector>
#include <stdexcept>
#include <string>
#include <cmath>

namespace mforce {

// PitchCurve — Score/Performer-level pitch contour for one note (or slide run).
//
//   semi[i]  = semitone offset from the note's (or run-anchor's) nominal pitch at node i
//   dur[i]   = fraction of total duration spent at semi[i].
//              Exactly one dur may be 0, meaning "fill remaining";
//              v1: zero must be the LAST index.
//   trans[i] = fractional width of the linear ramp straddling boundary i
//              (between node i and node i+1). Length N-1. Each width is split
//              half-and-half across the two flanking holds. v1: linear only.
//              If constructed via set_uniform_trans(), expands to a uniform vec.
//
// Performer builds this from Score intent (e.g. articulations::Bend, a Slide
// run) with its own timing choices; Instrument compiles to an Envelope and
// wires into the frequency ValueSource chain pre-render.

struct PitchCurve {
  std::vector<float> semi;
  std::vector<float> dur;
  std::vector<float> trans;  // length N-1 (one per boundary); empty for N==1

  // Convenience: set a uniform trans width across all boundaries.
  void set_uniform_trans(float t) {
    const size_t boundaries = semi.size() > 0 ? semi.size() - 1 : 0;
    trans.assign(boundaries, t);
  }

  // Strict validation — throw on any malformation. Caller handles.
  void validate() const {
    const size_t n = semi.size();
    if (n == 0 || n != dur.size())
      throw std::runtime_error("PitchCurve: semi[] and dur[] must be non-empty and equal length");
    if (trans.size() != (n > 0 ? n - 1 : 0))
      throw std::runtime_error("PitchCurve: trans[] must be length N-1");

    // Count zero-dur entries; v1 requires at most one, at the last index.
    int zeroCount = 0;
    int zeroIdx = -1;
    for (size_t i = 0; i < n; ++i) {
      if (dur[i] == 0.0f) { ++zeroCount; zeroIdx = int(i); }
      else if (dur[i] < 0.0f)
        throw std::runtime_error("PitchCurve: dur[" + std::to_string(i) + "] is negative");
    }
    if (zeroCount > 1)
      throw std::runtime_error("PitchCurve: at most one dur may be zero (fill)");
    if (zeroCount == 1 && zeroIdx != int(n) - 1)
      throw std::runtime_error("PitchCurve: zero dur (fill) must be the last index (v1)");

    // Sum check (or fill-room check).
    float sumNonZero = 0.0f;
    for (size_t i = 0; i < n; ++i) sumNonZero += dur[i];
    const float eps = 1e-5f;
    if (zeroCount == 0) {
      if (std::abs(sumNonZero - 1.0f) > eps)
        throw std::runtime_error("PitchCurve: dur[] sum must equal 1.0 (no fill entry)");
    } else {
      if (sumNonZero >= 1.0f - eps)
        throw std::runtime_error("PitchCurve: non-zero dur entries must sum to < 1.0 when fill present");
    }

    // Per-boundary trans: each side (trans[i]/2) must fit inside its flanking hold.
    for (size_t i = 0; i < trans.size(); ++i) {
      if (trans[i] < 0.0f)
        throw std::runtime_error("PitchCurve: trans[" + std::to_string(i) + "] is negative");
    }

    // Effective hold length per node = dur[i], with fill computed for the last
    // node when dur[last] == 0. Each node's hold absorbs half of each adjacent
    // trans: (trans[i-1]/2) from left, (trans[i]/2) from right.
    auto hold_at = [&](size_t i) {
      return (zeroCount == 1 && i == n - 1) ? (1.0f - sumNonZero) : dur[i];
    };
    for (size_t i = 0; i < n; ++i) {
      float avail = hold_at(i);
      float needed = 0.0f;
      if (i > 0)     needed += trans[i - 1] * 0.5f;
      if (i + 1 < n) needed += trans[i] * 0.5f;
      if (needed > avail + eps)
        throw std::runtime_error("PitchCurve: trans half-widths exceed dur at node " + std::to_string(i));
    }
  }
};

} // namespace mforce
