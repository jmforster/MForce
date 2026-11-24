#pragma once

namespace mforce::ui {

struct Session;

// The bottom pane under the canvas: which of its three tabs is shown, and
// whether it is collapsed to its tab bar. Not part of the patch.
struct Pane {
  enum class Tab { Waveforms, Spectrum, Keyboard };
  Tab  tab{Tab::Waveforms};
  bool collapsed{false};
};

void show_pane_tab(Session& s, Pane::Tab tab);
void collapse_pane(Session& s, bool collapsed);

}  // namespace mforce::ui
