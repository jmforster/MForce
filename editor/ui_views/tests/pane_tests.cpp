// The bottom pane under the headless editor: each tab clicked shows; a note
// key plays whatever tab is shown; the chevron collapses and restores it and
// the canvas takes the room; the spectrum names the frequency under the mouse.
#include "mforce/testing/check.h"
#include "mforce/ui_control/files.h"
#include "mforce/ui_control/generating.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_views/headless.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace mforce;
using namespace mforce::ui;

namespace {

const std::string kOboe = "patches/library/winds/oboe1.json";

}  // namespace

void run_pane_tab_tests() {
  std::printf("the pane: each tab clicked shows; a note key plays with Waveforms shown\n");
  Session s;
  open_file(s, kOboe);
  HeadlessEditor e(s);
  e.frames(3);
  CHECK(s.pane.tab == Pane::Tab::Waveforms && e.area("Fit").has_value() && !e.area("spectrum"));
  REQUIRE(e.click_on("tab:Spectrum"));
  CHECK(s.pane.tab == Pane::Tab::Spectrum && e.area("spectrum").has_value() && !e.area("Fit"));
  REQUIRE(e.click_on("tab:Keyboard"));
  CHECK(s.pane.tab == Pane::Tab::Keyboard && !e.area("spectrum"));
  REQUIRE(e.click_on("tab:Waveforms"));
  CHECK(s.pane.tab == Pane::Tab::Waveforms && e.area("Fit").has_value());
  e.press_key("Q");
  CHECK(s.lastNote.note == s.keyboard.octave * 12);                        // Q: the keyboard octave's C
}

void run_pane_collapse_tests() {
  std::printf("the pane: the chevron collapses it to its tab bar and restores it; the canvas takes the room\n");
  Session s;
  open_file(s, kOboe);
  HeadlessEditor e(s);
  e.frames(3);
  const float open = e.canvas_size().y;
  REQUIRE(e.click_on("chevron"));
  e.frames(1);
  CHECK(s.pane.collapsed && e.canvas_size().y > open + 150.0f);
  CHECK(e.area("tab:Spectrum").has_value() && !e.area("Fit"));             // the tab bar stays, no tab's content
  e.press_key("Q");
  CHECK(s.lastNote.note == s.keyboard.octave * 12);                        // the keys still play
  REQUIRE(e.click_on("chevron"));
  e.frames(1);
  CHECK(!s.pane.collapsed && e.canvas_size().y == open);
}

// After a Generate of a note, the readout under the mouse names the
// frequency at the mouse's place on the log axis.
void run_spectrum_readout_tests() {
  std::printf("the spectrum: the readout names the frequency under the mouse\n");
  Session s;
  open_file(s, kOboe);
  HeadlessEditor e(s);
  e.frames(3);
  REQUIRE(e.click_on("tab:Spectrum"));
  REQUIRE(e.hover_on("spectrum"));
  CHECK(e.spectrum_readout().empty());                                     // no render yet
  s.transport.note = "69";
  REQUIRE(e.click("Generate"));
  e.frames(3);
  const auto plot = e.area("spectrum");
  REQUIRE(plot.has_value());
  const double low = std::log10(20.0), high = std::log10(s.take.sampleRate / 2.0);
  const double share = (std::log10(440.0) - low) / (high - low);
  e.hover(Vec2{plot->lo.x + float(share) * (plot->hi.x - plot->lo.x), (plot->lo.y + plot->hi.y) * 0.5f});
  const std::string words = e.spectrum_readout();
  const double hz = std::atof(words.c_str());
  CHECK(words.find(" Hz   ") != std::string::npos && std::fabs(hz - 440.0) < 3.0);
  if (std::fabs(hz - 440.0) >= 3.0) std::printf("  readout: %s\n", words.c_str());
}
