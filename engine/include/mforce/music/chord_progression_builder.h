#pragma once
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/music/structure.h"
#include "mforce/core/randomizer.h"
#include <string>
#include <unordered_map>
#include <functional>
#include <vector>
#include <stdexcept>

namespace mforce {

// ---------------------------------------------------------------------------
// ChordProgressionBuilder — named-progression lookup and scaling.
//
// Throwaway-grade: hard-coded named lambdas, no context awareness (doesn't
// know the Piece key, doesn't consult prior Sections, doesn't plan
// modulations). Solves exactly one problem: "stop making every patch
// hand-author a 4-chord array in JSON."
//
// When/if we want pluggable context-aware progression generation, promote
// to a ChordProgressionStrategy with plan_* / compose_* pattern. For now,
// this is sufficient for melody-only openings like K467 bars 1-12.
//
// Renamed from HarmonyComposer (2026-04-15): original name over-promoted
// ("Composer" already means the orchestrator class) and "Harmony" is
// ambiguous (contrapuntal coincidence, voicing, inversions — none of
// which this type handles).
// ---------------------------------------------------------------------------
struct ChordProgressionBuilder {

  // Build a named progression scaled to fit the given number of beats.
  static ChordProgression build(const std::string& progressionName, float totalBeats) {
    auto it = progressions().find(progressionName);
    if (it == progressions().end()) {
      throw std::runtime_error("ChordProgressionBuilder: unknown progression '" + progressionName + "'");
    }
    return it->second(totalBeats);
  }

  // List available progression names
  static std::vector<std::string> available() {
    std::vector<std::string> names;
    for (const auto& [k, v] : progressions()) names.push_back(k);
    return names;
  }

private:
  using Builder = std::function<ChordProgression(float totalBeats)>;

  static const std::unordered_map<std::string, Builder>& progressions() {
    static const std::unordered_map<std::string, Builder> map = {

      // I - V7 - V7 - I  (K467 opening)
      {"I-V7-V7-I", [](float beats) {
        float bar = beats / 4.0f;
        ChordProgression prog;
        prog.add(0, "Major", bar);     // I
        prog.add(4, "7", bar);         // V7
        prog.add(4, "7", bar);         // V7
        prog.add(0, "Major", bar);     // I
        return prog;
      }},

      // I - IV - V - I  (basic classical)
      {"I-IV-V-I", [](float beats) {
        float bar = beats / 4.0f;
        ChordProgression prog;
        prog.add(0, "Major", bar);
        prog.add(3, "Major", bar);     // IV
        prog.add(4, "Major", bar);     // V
        prog.add(0, "Major", bar);     // I
        return prog;
      }},

      // I - V - vi - IV  (pop)
      {"I-V-vi-IV", [](float beats) {
        float bar = beats / 4.0f;
        ChordProgression prog;
        prog.add(0, "Major", bar);
        prog.add(4, "Major", bar);
        prog.add(5, "minor", bar);     // vi
        prog.add(3, "Major", bar);     // IV
        return prog;
      }},

      // ii - V - I  (jazz turnaround, 3 bars)
      {"ii-V-I", [](float beats) {
        float bar = beats / 3.0f;
        ChordProgression prog;
        prog.add(1, "minor", bar);     // ii
        prog.add(4, "7", bar);         // V7
        prog.add(0, "Major", bar);     // I
        return prog;
      }},

      // I - I - I - I  (single chord, for testing)
      {"I", [](float beats) {
        ChordProgression prog;
        prog.add(0, "Major", beats);
        return prog;
      }},
    };
    return map;
  }
};

} // namespace mforce
