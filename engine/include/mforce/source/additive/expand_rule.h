#pragma once
// ExpandRule: the sub-partial expansion rule a Partials host applies.

namespace mforce {

// ---------------------------------------------------------------------------
// ExpandRule — controls generation of "sub-partials" around each primary
// partial. Ported from legacy PartialsExpandRule.cs.
// ---------------------------------------------------------------------------
struct ExpandRule {
  int count{2};          // sub-partials per side
  int recurse{0};        // how many times to re-expand
  float spacing1{0.5f}, spacing2{0.5f};  // semitone spacing (start/end)
  float dt1{0.01f}, dt2{0.01f};          // random detune on sub-partials
  float loPct1{0.1f}, loPct2{0.1f};      // minimum amplitude fraction
  float power1{1.0f}, power2{1.0f};      // amplitude curve power
  float po1{0.0f}, po2{0.0f};            // phase offset for sub-partials
};

} // namespace mforce
