#pragma once
#include "mforce/music/structure.h"

namespace mforce {

// Writes each part's events from its passages. A part that already has
// events is left as it is.
//
// Each section's passage starts at section_start_beat and is cut at the
// section's played_beats: no note starts at or past the cut, and the last
// note to start before it ends on it.
void realize_events(Piece& piece);

} // namespace mforce
