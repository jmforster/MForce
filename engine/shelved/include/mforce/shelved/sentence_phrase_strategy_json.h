#pragma once
// The sentence phrase strategy's settings, to and from JSON.
#include "mforce/shelved/sentence_phrase_strategy.h"
#include "mforce/compose/templates_json.h"

namespace mforce {

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
