#pragma once
#include "mforce/music/voicing_profile_selector.h"
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// ScriptedVoicingProfileSelector — cycles through an authored profile
// sequence. Deterministic — no randomness. Use when you want
// "bar 1 tight, bar 2 loose, bar 3 open, bar 4 back home" as a repeating
// figure.
//
// Config (nlohmann::json):
//   sequence (list of profile objects):
//     [
//       { "priority": 0.0, "allowedInversions": [0, 1], "allowedSpreads": [0] },
//       { "priority": 0.5, "allowedInversions": [0, 1, 2], "allowedSpreads": [0, 1] },
//       ...
//     ]
// Walker returns sequence[chordIdx % sequence.size()] verbatim.
// ---------------------------------------------------------------------------
class ScriptedVoicingProfileSelector : public VoicingProfileSelector {
 public:
  std::string name() const override { return "scripted"; }

  void configure_from_json(const nlohmann::json& cfg) override {
    sequence_.clear();
    if (!cfg.contains("sequence") || !cfg["sequence"].is_array()) return;
    for (const auto& pj : cfg["sequence"]) {
      VoicingProfile p;
      p.priority = pj.value("priority", 0.0f);
      if (pj.contains("allowedInversions") && pj["allowedInversions"].is_array()) {
        for (const auto& v : pj["allowedInversions"]) {
          p.allowedInversions.push_back(v.get<int>());
        }
      }
      if (pj.contains("allowedSpreads") && pj["allowedSpreads"].is_array()) {
        for (const auto& v : pj["allowedSpreads"]) {
          p.allowedSpreads.push_back(v.get<int>());
        }
      }
      sequence_.push_back(std::move(p));
    }
  }

  void reset(uint32_t /*seed*/) override {}  // deterministic; no RNG

  VoicingProfile profile_for_chord(int chordIdx,
                                   float /*beatInBar*/,
                                   float /*beatInPassage*/) override {
    if (sequence_.empty()) return VoicingProfile{};
    int idx = chordIdx % int(sequence_.size());
    if (idx < 0) idx += int(sequence_.size());
    return sequence_[idx];
  }

 private:
  std::vector<VoicingProfile> sequence_;
};

}  // namespace mforce
