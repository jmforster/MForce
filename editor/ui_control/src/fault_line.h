#pragma once
#include <string>
#include <vector>
#include "mforce/render/fault_report.h"

// What nodes reported, as one status line, shared by the control's files.
namespace mforce::ui {

// "faults: osc (SineSource): frequency clamped; ...", each in the engine's
// words; empty when there are none.
inline std::string fault_line(const std::vector<FaultReport>& reports) {
  if (reports.empty()) return {};
  std::string text = "faults: ";
  for (size_t k = 0; k < reports.size(); ++k) text += (k ? "; " : "") + fault_report_text(reports[k]);
  return text;
}

}  // namespace mforce::ui
