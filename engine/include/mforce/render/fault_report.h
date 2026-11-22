#pragma once
#include "mforce/core/dsp_value_source.h"
#include <string>

namespace mforce {

// One node's faults, as a host reports them after a render.
struct FaultReport {
  std::string nodeId;
  const char* type;
  unsigned char faults;
};

// "osc1 (SineSource): frequency clamped, table length clamped" — the one
// wording every host prints.
inline std::string fault_report_text(const FaultReport& r) {
  std::string s = r.nodeId + " (" + r.type + "):";
  bool first = true;
  for (unsigned char bit = 1; bit; bit = static_cast<unsigned char>(bit << 1)) {
    if (!(r.faults & bit)) continue;
    s += first ? " " : ", ";
    s += fault_name(static_cast<Fault>(bit));
    first = false;
  }
  return s;
}

} // namespace mforce
