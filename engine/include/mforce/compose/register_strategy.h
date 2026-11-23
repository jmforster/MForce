#pragma once
#include "mforce/compose/compose.h"
#include "mforce/compose/templates_json.h"
#include <memory>

namespace mforce {

// A strategy a template can name: in the kit and in the codecs. The one
// line each registration list (register_builtin, register_shelved) writes
// per strategy.
template <class Strategy>
void register_strategy(ComposeKit& kit, SettingsCodecs& codecs) {
  kit.strategies.add(std::make_unique<Strategy>());
  codecs.add<Strategy>();
}

} // namespace mforce
