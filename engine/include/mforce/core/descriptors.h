#pragma once

namespace mforce {

// ---------------------------------------------------------------------------
// Self-description types: what a pin says about itself to the UI and the
// patch codec. A node's pins build these from their declarations (pins.h).
// ---------------------------------------------------------------------------

enum class SourceCategory {
  Oscillator,   // SineSource, SawSource, PulseSource, FMSource, WavetableSource, HybridKS...
  Generator,    // RedNoiseSource, WanderNoise*, WhiteNoise, SegmentSource...
  Modulator,    // VarSource, RangeSource, RepeatingSource, Vibrato...
  Envelope,     // Envelope, PhasedValueSource
  Filter,       // BWLowpass, BWHighpass, BWBandpass, Delay
  Combiner,     // CombinedSource, CrossfadeSource, MultiSource
  Additive,     // AdditiveSource, FullAdditiveSource, Formant...
  Utility       // ConstantSource, StaticVarSource, StaticRangeSource
};

struct ParamDescriptor {
  const char* name;
  float default_value;
  float min_value;
  float max_value;
  // Optional UI advisory tag. Short, shown dim next to the pin name in the
  // inspector (e.g. "hz", "0-1", "semi", "cycles"). Advisory only — no
  // enforcement at connect-time. Leave null when the param's name already
  // tells you the expected range.
  const char* hint = nullptr;
};

struct InputDescriptor {
  const char* name;
  bool multi{false};  // true = accepts multiple connections (e.g. spectra, stages)
  // Optional UI advisory tag — same semantics as ParamDescriptor::hint.
  const char* hint = nullptr;
};

enum class SettingType { Float, Int, Bool };

struct SettingDescriptor {
  const char* name;
  SettingType type;
  float default_value;
  float min_value;
  float max_value;   // for Float/Int; ignored for Bool
  // Optional: null-terminated array of display labels for Int settings that
  // represent an enum. When set, the UI renders a dropdown instead of a
  // numeric input. The stored value remains the integer index.
  const char* const* enum_labels = nullptr;
};

// Array-of-floats params. The UI groups arrays sharing a non-null groupName
// into a single parallel-columns table (e.g. ExplicitPartials has four arrays
// — mult1/mult2/ampl1/ampl2 — all grouped as "partials" and kept equal length).
// Standalone arrays (groupName == nullptr) render as a single editable list.
struct ArrayDescriptor {
  const char* name;
  const char* groupName;      // nullptr = standalone; non-null = table group key
  float default_value;        // value for newly appended rows
  float min_value;
  float max_value;
};

} // namespace mforce
