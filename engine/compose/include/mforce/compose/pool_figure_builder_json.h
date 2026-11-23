#pragma once
// The Markov atom pool from a JSON value.
#include "mforce/compose/pool_figure_builder.h"
#include <nlohmann/json.hpp>

namespace mforce {

// The pool's "atoms": each one's units, noteCount, totalBeats, contour and
// count, all required.
PoolFigureBuilder pool_from_json(const nlohmann::json& poolJson, uint32_t seed);

} // namespace mforce
