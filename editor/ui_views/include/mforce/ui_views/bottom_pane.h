#pragma once
#include <optional>
#include <string>
#include "mforce/ui_control/session.h"
#include "mforce/ui_views/keyboard_view.h"
#include "mforce/ui_views/places.h"
#include "mforce/ui_views/spectrum_view.h"
#include "mforce/ui_views/waveforms_view.h"

namespace mforce::ui {

// The pane across the window under the canvas: three tabs, Waveforms (shown
// first), Spectrum and Keyboard, the tab shown session.pane's; and at the tab
// bar's right end an arrow that collapses the pane to its tab bar (down) and
// restores it (up). The computer's keys play whatever the pane shows.
class BottomPane {
public:
  // The pane's height in a window whose room under the menu bar is `room`
  // high: 0.28 of it, or the tab bar's alone while collapsed.
  static float height(const Session& s, float room);
  void begin_frame();                // before anything is drawn this frame
  void draw(Session& s);             // inside the pane's child window
  void draw_pop_outs(Session& s) { waves_.draw_pop_outs(s); }
  bool over_strips() const { return waves_.over(); }
  Vec2 piano_origin() const { return keyboard_.piano_origin(); }
  const std::string& spectrum_readout() const { return spectrum_.readout(); }
  // Where a named thing was drawn last frame: "tab:Waveforms", "tab:Spectrum",
  // "tab:Keyboard", "chevron", and every name the waveforms and the spectrum
  // keep (WaveformsView::places, SpectrumView::places).
  std::optional<Area> area(const std::string& name) const;

private:
  void tabs(Session& s);
  void chevron(Session& s, Vec2 barEnd);

  KeyboardView keyboard_;
  WaveformsView waves_;
  SpectrumView spectrum_;
  Places places_;
};

}  // namespace mforce::ui
