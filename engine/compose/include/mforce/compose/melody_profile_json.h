#pragma once
// The melody profile from a JSON value (the file's top level).
#include "mforce/compose/melody_profile.h"
#include <nlohmann/json.hpp>

namespace mforce {

// Strict: every key is required and a missing one throws by name.
MelodyProfile parse_melody_profile(const nlohmann::json& file);

} // namespace mforce
