#pragma once
#include "mforce/compose/compose.h"

namespace mforce {

class SettingsCodecs;  // mforce/compose/templates_json.h

// Registers the eleven shelved strategies in the kit and their names and
// settings in the codecs. A host that registers both this and
// register_builtin has every strategy the engine has.
void register_shelved(ComposeKit& kit, SettingsCodecs& codecs);

} // namespace mforce
