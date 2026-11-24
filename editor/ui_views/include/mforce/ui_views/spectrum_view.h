#pragma once
#include <string>
#include "mforce/ui_control/session.h"
#include "mforce/ui_control/spectrum.h"
#include "mforce/ui_views/places.h"

namespace mforce::ui {

// The pane's Spectrum tab: the last render's spectrum, at its own rate, on a
// log axis from 20 Hz to half the rate and from -100 to +6 dB, with its grid
// and labels; under the mouse a crosshair and the frequency and level there.
// "No render yet — hit Generate." before a render. It makes the spectrum
// once for each take (session.takes) and keeps it.
class SpectrumView {
public:
  void begin_frame();                // before the pane draws: what it drew last frame is put aside
  void draw(const Session& s);
  // The readout under the mouse as last drawn ("440.0 Hz   -0.1 dB"); empty when the mouse was not on the plot.
  const std::string& readout() const { return readout_; }
  const Places& places() const { return places_; }   // "spectrum": the plot

private:
  Spectrum spectrum_;
  int madeFor_{-1};                  // the take the spectrum was made from
  std::string readout_;
  Places places_;
};

}  // namespace mforce::ui
