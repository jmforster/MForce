#pragma once
#include "mforce/music/basics.h"
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace mforce {

// ---------------------------------------------------------------------------
// VoicingRequest — inputs to a VoicingSelector. Captures the abstract chord
// to voice plus any contextual hints the selector may use to score candidates.
// ---------------------------------------------------------------------------
struct VoicingRequest {
  ScaleChord scaleChord;              // what to voice (degree + alt + quality)
  const Scale* scale{nullptr};        // interpretive context for degree -> pitch
  int rootOctave{3};                  // where to place the root
  float durationBeats{1.0f};          // passes through to Chord.dur
  const Chord* previous{nullptr};     // prior chord on this Part (for voice-leading)
  std::optional<Pitch> melodyPitch;   // optional top-voice hint (future: melody-aware)
  float priority{0.0f};               // [0,1]: 0=pure VL distance, 1=pure common tones
  std::string dictionaryName;         // ChordDictionary name; empty = Canonic
};

// ---------------------------------------------------------------------------
// VoicingSelector — resolves a ScaleChord into a concrete Chord with pitches.
// Implementations differ in which dictionaries they search and how they score
// candidates (voice-leading distance, melody clash, style preference, etc.).
// ---------------------------------------------------------------------------
class VoicingSelector {
 public:
  virtual ~VoicingSelector() = default;
  virtual std::string name() const = 0;
  virtual Chord select(const VoicingRequest& req) = 0;
};

// ---------------------------------------------------------------------------
// VoicingSelectorRegistry — registers named selectors for lookup by name.
// Parallel to StrategyRegistry. Populated at Composer construction time.
// ---------------------------------------------------------------------------
class VoicingSelectorRegistry {
 public:
  static VoicingSelectorRegistry& instance() {
    static VoicingSelectorRegistry inst;
    return inst;
  }

  void register_selector(std::unique_ptr<VoicingSelector> s) {
    std::string n = s->name();
    selectors_[n] = std::move(s);
  }

  VoicingSelector* resolve(const std::string& n) const {
    auto it = selectors_.find(n);
    return (it != selectors_.end()) ? it->second.get() : nullptr;
  }

 private:
  std::unordered_map<std::string, std::unique_ptr<VoicingSelector>> selectors_;
};

}  // namespace mforce
