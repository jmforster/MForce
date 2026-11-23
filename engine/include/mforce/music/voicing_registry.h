#pragma once
#include "mforce/music/voicing_profile_selector.h"
#include "mforce/music/voicing_selector.h"
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace mforce {

// VoicingRegistry — the voicing selectors a host has registered, by name,
// and one factory per voicing profile selector. A profile selector keeps
// state for one passage, so each request for one builds a fresh instance.
//
// A name that is not registered throws std::runtime_error naming it. An
// empty profile selector name means "static". A voicing selector has no
// default: a passage that names none voices its chords by inversion and
// spread, and does not ask this registry.
class VoicingRegistry {
public:
  using ProfileSelectorFactory =
      std::function<std::unique_ptr<VoicingProfileSelector>()>;

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
  make_profile_selector(std::string_view name) const {
    const std::string key = name.empty() ? std::string("static")
                                         : std::string(name);
    auto it = profileFactories_.find(key);
    if (it == profileFactories_.end()) {
      throw std::runtime_error("no voicing profile selector named '" + key +
                               "' is registered");
    }
    return it->second();
  }

private:
  std::unordered_map<std::string, std::unique_ptr<VoicingSelector>> selectors_;
  std::unordered_map<std::string, ProfileSelectorFactory> profileFactories_;
};

} // namespace mforce
