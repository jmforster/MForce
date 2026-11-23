#pragma once
// The period and sentence phrase strategies' settings, to and from JSON.
#include "mforce/music/phrase_strategies.h"
#include "mforce/music/templates_json.h"

namespace mforce {

// ===========================================================================
// PeriodPhraseSettings
// ===========================================================================

inline void to_json(json& j, const PeriodPhraseSettings& c) {
  j["basicIdea"] = c.basicIdea;
  j["antecedentTail"] = c.antecedentTail;
  j["consequentTail"] = c.consequentTail;
  if (c.halfCadenceTarget != 4) j["halfCadenceTarget"] = c.halfCadenceTarget;
}

inline void from_json(const json& j, PeriodPhraseSettings& c) {
  from_json(j.at("basicIdea"), c.basicIdea);
  from_json(j.at("antecedentTail"), c.antecedentTail);
  from_json(j.at("consequentTail"), c.consequentTail);
  c.halfCadenceTarget = j.value("halfCadenceTarget", 4);
}

// ===========================================================================
// SentencePhraseSettings
// ===========================================================================

inline void to_json(json& j, const SentencePhraseSettings& c) {
  j["basicIdea"] = c.basicIdea;
  j["continuation"] = c.continuation;
  if (c.variationTransposition != 0) j["variationTransposition"] = c.variationTransposition;
}

inline void from_json(const json& j, SentencePhraseSettings& c) {
  from_json(j.at("basicIdea"), c.basicIdea);
  from_json(j.at("continuation"), c.continuation);
  c.variationTransposition = j.value("variationTransposition", 0);
}

} // namespace mforce
