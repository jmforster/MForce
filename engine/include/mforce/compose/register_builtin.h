#pragma once
#include "mforce/compose/compose.h"

namespace mforce {

class SettingsCodecs;  // templates_json.h

// Registers compose's own strategies, voicing selector and voicing profile
// selectors in the kit, and each one a template can name in the codecs, from
// one list, so a strategy is registered once. The shelved strategies
// register through register_shelved (mforce/shelved/register_shelved.h).
void register_builtin(ComposeKit& kit, SettingsCodecs& codecs);

} // namespace mforce
