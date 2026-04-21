#pragma once
#include "mforce/music/voicing_profile.h"
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

namespace mforce {

// ---------------------------------------------------------------------------
// VoicingProfileSelector — produces a VoicingProfile per chord position.
// The per-Passage "selector layer above the VoicingSelector": varies the
// profile (priority, inversion allow-list, spread allow-list) chord-by-
// chord under author-configurable policies.
//
// Implementations ship with their own per-chord logic:
//   StaticVoicingProfileSelector   — returns the same profile every chord
//   RandomVoicingProfileSelector   — samples independently per chord
//   DriftVoicingProfileSelector    — smooth random walk
//   ScriptedVoicingProfileSelector — cycles through an authored sequence
// ---------------------------------------------------------------------------
class VoicingProfileSelector {
 public:
  virtual ~VoicingProfileSelector() = default;
  virtual std::string name() const = 0;
  virtual void configure_from_json(const nlohmann::json& /*cfg*/) {}
  virtual void reset(uint32_t seed) = 0;

  // Called per chord in chord order. `beatInBar` and `beatInPassage` are
  // float beat positions (from bar start and from passage start).
  virtual VoicingProfile profile_for_chord(int chordIdx,
                                           float beatInBar,
                                           float beatInPassage) = 0;
};

// ---------------------------------------------------------------------------
// VoicingProfileSelectorRegistry — factory-per-name registry. Profile
// selectors carry per-Passage state, so the registry creates fresh
// instances rather than sharing singletons.
// ---------------------------------------------------------------------------
class VoicingProfileSelectorRegistry {
 public:
  using Factory = std::function<std::unique_ptr<VoicingProfileSelector>()>;

  static VoicingProfileSelectorRegistry& instance() {
    static VoicingProfileSelectorRegistry inst;
    return inst;
  }

  void register_factory(const std::string& name, Factory f) {
    factories_[name] = std::move(f);
  }

  std::unique_ptr<VoicingProfileSelector> create(const std::string& name) const {
    auto it = factories_.find(name);
    if (it == factories_.end()) return nullptr;
    return it->second();
  }

 private:
  std::unordered_map<std::string, Factory> factories_;
};

}  // namespace mforce
