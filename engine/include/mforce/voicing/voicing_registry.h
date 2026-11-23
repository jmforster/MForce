#pragma once
#include "mforce/voicing/voicing_profile_selector.h"
#include "mforce/voicing/voicing_selector.h"
#include <any>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace mforce {

// VoicingRegistry — the voicing selectors a host has registered, by name,
// and one factory per voicing profile selector. A profile selector keeps
// state for one passage, so each request for one builds a fresh instance,
// from the passage's baseline profile and the settings the passage gives
// that selector (empty = its defaults).
//
// A name that is not registered throws std::runtime_error naming it. An
// empty profile selector name means "static". A voicing selector has no
// default: a passage that names none voices its chords by inversion and
// spread, and does not ask this registry.
class VoicingRegistry {
public:
  using ProfileSelectorFactory = std::function<std::unique_ptr<VoicingProfileSelector>(
      const VoicingProfile& baseline, const std::any& settings)>;

  void add(std::unique_ptr<VoicingSelector> s) {
    const std::string name = s->name();
    selectors_[name] = std::move(s);
  }

  void add_profile_selector(const std::string& name,
                            ProfileSelectorFactory factory) {
    profileFactories_[name] = std::move(factory);
  }

  VoicingSelector& selector(std::string_view name) const {
    auto it = selectors_.find(std::string(name));
    if (it == selectors_.end()) {
      throw std::runtime_error("no voicing selector named '" +
                               std::string(name) + "' is registered");
    }
    return *it->second;
  }

  std::unique_ptr<VoicingProfileSelector>
  make_profile_selector(std::string_view name, const VoicingProfile& baseline,
                        const std::any& settings) const {
    const std::string key = name.empty() ? std::string("static")
                                         : std::string(name);
    auto it = profileFactories_.find(key);
    if (it == profileFactories_.end()) {
      throw std::runtime_error("no voicing profile selector named '" + key +
                               "' is registered");
    }
    return it->second(baseline, settings);
  }

private:
  std::unordered_map<std::string, std::unique_ptr<VoicingSelector>> selectors_;
  std::unordered_map<std::string, ProfileSelectorFactory> profileFactories_;
};

} // namespace mforce
