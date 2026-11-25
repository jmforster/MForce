#pragma once
#include <map>
#include <vector>
#include "mforce/ui_control/session.h"
#include "mforce/ui_control/waveforms.h"
#include "mforce/ui_views/places.h"

namespace mforce::ui {

// The waveform strips of the last render: in the pane's Waveforms tab, a
// toolbar (the zoom arrows and the samples a column shows, Fit, Columns,
// Envelopes, and the scroll slider when there is more to see) over the
// strips in one to four columns, each with an arrow that pops it out; and
// each strip popped out, in a window of its own named by its number, with
// its own zoom, Fit and scroll. Over strips, here or popped out: the wheel
// scrolls, Ctrl and the wheel zoom, a left drag scrolls, and the strips'
// keys (the arrows, Home, End, =, -, 0) act. Every amount is the control's
// (waveforms.h); this view reports what the hand did and draws. It keeps the
// tables the strips are drawn from (an ExtentCache) and where it drew what.
class WaveformsView {
public:
  void draw(Session& s);             // the tab's toolbar and strips; only while the tab is shown
  void draw_pop_outs(Session& s);    // after the main window: each pop-out's window
  void begin_frame();                // before either: what was drawn and hovered last frame is put aside
  // The mouse was over the pane's strips or a pop-out's this frame.
  bool over() const;
  // Where each named thing was drawn last frame: "zoom in", "zoom out",
  // "Fit", "columns -", "columns", "columns +", "Envelopes", "scroll",
  // "strip:<label>", "arrow:<label>", and per pop-out "popout <n>:strip",
  // "popout <n>:close", "popout <n>:zoom in", "popout <n>:zoom out",
  // "popout <n>:Fit", "popout <n>:scroll".
  const Places& places() const { return places_; }

private:
  void toolbar(Session& s, float width);
  void strips(Session& s);
  bool pop_out(Session& s, PopOut& p, int order);   // false when its close button was pressed

  ExtentCache cache_;
  std::vector<Extent> columns_;      // the room each strip's columns are worked out in
  Places places_;
  int plotWidth_{0};                 // one column's plot, as last drawn
  bool overPane_{false}, wasOverPane_{false};
  std::map<int, bool> overPop_, wasOverPop_;   // by pop-out number
};

}  // namespace mforce::ui
