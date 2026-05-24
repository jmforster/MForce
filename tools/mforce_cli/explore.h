#pragma once

namespace mforce {

// mforce_cli --explore <spec.json> [--tag <name>]
//
// Batch parameter-sweep rendering for sound exploration. Takes a sweep spec
// (base patch + axes + ranges), generates the cross product of all axis
// combinations, renders each variant to renders/explore/<run-id>/, and
// writes a manifest.json with per-variant params and signal stats
// (peak, RMS, ZCR, spectral centroid, spectral flatness).
//
// See docs (or the spec parser below) for the spec JSON shape.
int run_explore(int argc, char** argv);

} // namespace mforce
