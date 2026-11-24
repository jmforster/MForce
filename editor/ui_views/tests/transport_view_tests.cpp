// The transport under the headless editor: each tab drawn on an instrument
// and on a plain sound leaves the patch as it was; a tab clicked becomes the
// mode; the Generate button's request renders after a frame; Space plays and
// Space again stops.
#include "mforce/testing/check.h"
#include "mforce/ui_control/files.h"
#include "mforce/ui_control/generating.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_views/headless.h"
#include <cstdio>
#include <string>
#include <vector>

using namespace mforce;
using namespace mforce::ui;

namespace {

using Mode = Transport::Mode;
const std::string kOboe = "patches/library/winds/oboe1.json";
const std::string kPlain = "patches/baselines/fm_bell_test.json";

// The output for `buffers` buffers, read as the device would.
void run_output(Session& s, int buffers) {
  std::vector<float> out(2 * 512);
  for (int k = 0; k < buffers; ++k) s.audio.fill(out.data(), 512);
}

}  // namespace

// Each tab drawn, on an instrument and on a plain sound: the patch, its
// unsaved flag and the take are as they were.
void run_transport_tabs_draw_tests() {
  std::printf("the transport: each tab drawn on an instrument and a plain sound changes nothing\n");
  for (const std::string& path : {kOboe, kPlain}) {
    Session s;
    open_file(s, path);
    for (const Mode mode : {Mode::Note, Mode::Passage, Mode::Chords, Mode::Drums}) {
      Transport t = s.transport;
      t.mode = mode;
      set_transport(s, t);
      const Patch before = s.open.patch;
      draw_once_headless(s);
      CHECK(s.open.patch == before && !unsaved(s.open) && s.take.stereo.empty() && s.transport.mode == mode);
    }
  }
}

// A tab clicked becomes the mode; Generate clicked reads "Generating..." on
// the next frame and renders after it; Space plays, Space again stops.
void run_transport_click_tests() {
  std::printf("the transport: a tab clicked, Generate's request, Space to play and stop\n");
  Session s;
  open_file(s, kOboe);
  HeadlessEditor editor(s);
  editor.frames(3);
  CHECK(editor.click("Passage") && s.transport.mode == Mode::Passage);
  CHECK(editor.click("Note") && s.transport.mode == Mode::Note);
  s.keyboard.seconds = 0.3f;
  editor.frames(1);
  CHECK(!editor.click("Generating..."));
  CHECK(editor.click("Generate") && generate_requested(s) && s.take.stereo.empty());
  editor.frames(1);                                       // "Generating..." drawn and shown, then the render
  CHECK(!s.take.stereo.empty() && s.status == "Generated note" && !generate_requested(s));
  stop(s);
  s.take = Take{};
  request_generate(s);
  editor.frames(1);
  CHECK(s.take.stereo.empty() && generate_requested(s));  // the frame that says so is shown first
  editor.frames(1);
  CHECK(!s.take.stereo.empty() && !generate_requested(s));

  editor.press_space();
  CHECK(playing(s) && s.audio.playing(AudioAdapter::Line::Playback) != nullptr);
  key_down(s, 1, 62);
  run_output(s, 4);
  editor.press_space();
  run_output(s, 4);
  CHECK(!playing(s) && sounding_voices(s) == 0);
  editor.frames(1);                                       // a frame after the key, as any hand leaves
  CHECK(editor.click("Play") && playing(s));
  editor.frames(1);
  CHECK(editor.click("Stop") && !playing(s));
}
