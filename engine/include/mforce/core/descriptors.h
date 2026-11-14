#pragma once

#include <cctype>
#include <string_view>

namespace mforce {

// ---------------------------------------------------------------------------
// Self-description types: what a pin says about itself to the UI and the
// patch codec. A node's pins build these from their declarations (pins.h).
// ---------------------------------------------------------------------------

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
  // Optional UI advisory tag — same semantics as ParamDescriptor::hint. On a
  // slot, what the slot takes ("a Formant part"): the loader names it when
  // it refuses a wire.
  const char* hint = nullptr;
  bool slot = false;  // a structural input: a part, not a signal
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
  // The setting sizes or reshapes the node (SettingBase::structural): it
  // takes no per-note push, so the UI offers it no pin and the loader
  // refuses a wire into it.
  bool structural = false;
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
  // Optional: null-terminated array of labels for a column that represents an
  // enum. When set, the column holds indices into the labels and the patch
  // file holds the labels.
  const char* const* enum_labels = nullptr;
};

// The one label match for an enum setting and an enum column: the index of
// the label equal to `s` ignoring ASCII case, whole-string; -1 when none is,
// or when there are no labels.
inline int enum_index(const char* const* labels, std::string_view s) {
  if (!labels) return -1;
  for (int i = 0; labels[i]; ++i) {
    const std::string_view label = labels[i];
    if (label.size() != s.size()) continue;
    bool same = true;
    for (size_t k = 0; same && k < s.size(); ++k)
      same = std::tolower(static_cast<unsigned char>(label[k])) ==
             std::tolower(static_cast<unsigned char>(s[k]));
    if (same) return i;
  }
  return -1;
}

// The label at index `i`, or null when `i` is outside the labels or there
// are no labels.
inline const char* enum_label(const char* const* labels, int i) {
  if (!labels || i < 0) return nullptr;
  for (int k = 0; labels[k]; ++k)
    if (k == i) return labels[k];
  return nullptr;
}

} // namespace mforce
