#pragma once
// The period phrase strategy's settings, to and from JSON.
#include "mforce/shelved/period_phrase_strategy.h"
#include "mforce/compose/templates_json.h"

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

} // namespace mforce
