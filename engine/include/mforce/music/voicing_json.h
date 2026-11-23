#pragma once
// The voicing profile and the voicing profile selectors' settings, to and
// from JSON. The selectors' readers are lenient as the selectors have
// always been: an absent key keeps its default, a profile list that is not
// an array is ignored, and an entry of one that is not an array is skipped.
#include "mforce/music/drift_voicing_profile_selector.h"
#include "mforce/music/random_voicing_profile_selector.h"
#include "mforce/music/scripted_voicing_profile_selector.h"
#include "mforce/music/voicing_profile.h"
#include <nlohmann/json.hpp>
#include <vector>

namespace mforce {

// A passage's baseline profile: every field that is not its default.
inline void to_json(nlohmann::json& j, const VoicingProfile& vp) {
  j = nlohmann::json::object();
  if (vp.priority != 0.0f) j["priority"] = vp.priority;
  if (!vp.allowedInversions.empty()) j["allowedInversions"] = vp.allowedInversions;
  if (!vp.allowedSpreads.empty()) j["allowedSpreads"] = vp.allowedSpreads;
  if (vp.repeatPenalty != 0.0f) j["repeatPenalty"] = vp.repeatPenalty;
  if (vp.cadential) j["cadential"] = vp.cadential;
}

inline void from_json(const nlohmann::json& j, VoicingProfile& vp) {
  vp.priority = j.value("priority", 0.0f);
  vp.repeatPenalty = j.value("repeatPenalty", 0.0f);
  vp.cadential = j.value("cadential", false);
  if (j.contains("allowedInversions")) {
    vp.allowedInversions = j.at("allowedInversions").get<std::vector<int>>();
  }
  if (j.contains("allowedSpreads")) {
    vp.allowedSpreads = j.at("allowedSpreads").get<std::vector<int>>();
  }
}

namespace voicing_json_detail {

// A list of int lists; a value that is not a list of lists reads as far as
// it is one.
inline std::vector<std::vector<int>> read_profiles(const nlohmann::json& cfg,
                                                   const char* key) {
  std::vector<std::vector<int>> profiles;
  if (cfg.contains(key) && cfg[key].is_array()) {
    for (const auto& pj : cfg[key]) {
      if (!pj.is_array()) continue;
      std::vector<int> prof;
      for (const auto& v : pj) prof.push_back(v.get<int>());
      profiles.push_back(std::move(prof));
    }
  }
  return profiles;
}

} // namespace voicing_json_detail

inline void from_json(const nlohmann::json& cfg, RandomProfileSettings& s) {
  s.priorityMin = cfg.value("priorityMin", 0.0f);
  s.priorityMax = cfg.value("priorityMax", 1.0f);
  s.inversionProfiles = voicing_json_detail::read_profiles(cfg, "inversionProfiles");
  s.spreadProfiles = voicing_json_detail::read_profiles(cfg, "spreadProfiles");
}

inline void to_json(nlohmann::json& j, const RandomProfileSettings& s) {
  j = nlohmann::json{{"priorityMin", s.priorityMin}, {"priorityMax", s.priorityMax}};
  if (!s.inversionProfiles.empty()) j["inversionProfiles"] = s.inversionProfiles;
  if (!s.spreadProfiles.empty()) j["spreadProfiles"] = s.spreadProfiles;
}

inline void from_json(const nlohmann::json& cfg, DriftProfileSettings& s) {
  s.priorityMin = cfg.value("priorityMin", 0.0f);
  s.priorityMax = cfg.value("priorityMax", 1.0f);
  s.priorityStepMax = cfg.value("priorityStepMax", 0.15f);
  s.profileTransitionProb = cfg.value("profileTransitionProb", 0.2f);
  s.inversionProfiles = voicing_json_detail::read_profiles(cfg, "inversionProfiles");
  s.spreadProfiles = voicing_json_detail::read_profiles(cfg, "spreadProfiles");
}

inline void to_json(nlohmann::json& j, const DriftProfileSettings& s) {
  j = nlohmann::json{{"priorityMin", s.priorityMin}, {"priorityMax", s.priorityMax},
                     {"priorityStepMax", s.priorityStepMax},
                     {"profileTransitionProb", s.profileTransitionProb}};
  if (!s.inversionProfiles.empty()) j["inversionProfiles"] = s.inversionProfiles;
  if (!s.spreadProfiles.empty()) j["spreadProfiles"] = s.spreadProfiles;
}

// Each entry of `sequence` is a profile; an entry reads its priority and
// its two allow-lists, a list that is not an array reading as empty.
inline void from_json(const nlohmann::json& cfg, ScriptedProfileSettings& s) {
  s.sequence.clear();
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
    s.sequence.push_back(std::move(p));
  }
}

inline void to_json(nlohmann::json& j, const ScriptedProfileSettings& s) {
  nlohmann::json seq = nlohmann::json::array();
  for (const auto& p : s.sequence) {
    nlohmann::json pj = nlohmann::json{{"priority", p.priority}};
    if (!p.allowedInversions.empty()) pj["allowedInversions"] = p.allowedInversions;
    if (!p.allowedSpreads.empty()) pj["allowedSpreads"] = p.allowedSpreads;
    seq.push_back(std::move(pj));
  }
  j = nlohmann::json{{"sequence", std::move(seq)}};
}

} // namespace mforce
