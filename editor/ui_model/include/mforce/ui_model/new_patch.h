#pragma once
#include <cstdint>
#include "mforce/patch/patch.h"

namespace mforce::ui {

// The patch File > New gives: an instrument holding a Sine ("Sine") and an
// Envelope ("Envelope"), unwired, with no output, laid out in a row with the
// Output node, the instrument block at its defaults. Made through the
// engine's edits.
Patch new_patch(std::uint32_t sineSeed, std::uint32_t envelopeSeed);

}  // namespace mforce::ui
