#pragma once
#include "mforce/music/compose.h"

namespace mforce {

class SettingsCodecs;  // templates_json.h

// Registers every built-in strategy, voicing selector and voicing profile
// selector in the kit, and each one a template can name in the codecs, from
// one list, so a strategy is registered once.
void register_builtin(ComposeKit& kit, SettingsCodecs& codecs);

} // namespace mforce
