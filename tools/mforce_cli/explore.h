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

// mforce_cli --explore-filter <manifest.json> [filters] [--sort <field>] [--limit N] [--json]
//
// Filters and ranks variants from an --explore manifest.json. Each filter
// flag takes a stat name and a min/max bound:
//   --peak-min/--peak-max
//   --rms-min/--rms-max
//   --zcr-min/--zcr-max
//   --centroid-min/--centroid-max
//   --flatness-min/--flatness-max
//
// Sorts by --sort <field> descending (peak, rms, zcr, centroid, flatness).
// --limit N caps output. --json emits a JSON array instead of a text table.
int run_explore_filter(int argc, char** argv);

} // namespace mforce
