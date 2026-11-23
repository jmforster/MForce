#pragma once
#include "mforce/music/compose.h"

namespace mforce {

// Registers every built-in strategy, voicing selector and voicing profile
// selector in the kit.
void register_builtin(ComposeKit& kit);

} // namespace mforce
