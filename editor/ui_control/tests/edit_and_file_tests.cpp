// The session's edits, files and unsaved guard, and the live rate; sound
// read by calling session.audio.fill() directly, never a device.
#include "mforce/testing/check.h"
#include "core/tests/fixtures.h"
#include "mforce/core/source_registry.h"
#include "mforce/patch/patch_json.h"
#include "mforce/source/register_all_sources.h"
#include "mforce/ui_control/edits.h"
#include "mforce/ui_control/files.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_control/unsaved.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace mforce;
using namespace mforce::ui;

namespace {

const std::string kOboe = "patches/library/winds/oboe1.json";
const std::string kSlowRate = "patches/baselines/double_advance_bug.json";   // a file at 22050 Hz

std::vector<float> buffer(Session& s, int frames = 256) {
  std::vector<float> out(size_t(2 * frames));
  s.audio.fill(out.data(), frames);
  return out;
}

bool sounds(Session& s, int buffers) {
  bool any = false;
  for (int k = 0; k < buffers; ++k) {
    const auto b = buffer(s);
    any = any || std::any_of(b.begin(), b.end(), [](float x) { return x != 0.0f; });
  }
  return any;
}

// A folder under the system's temporary folder, deleted with its files at the end.
struct TempFolder {
  std::filesystem::path path;
  explicit TempFolder(const std::string& name)
      : path(std::filesystem::temp_directory_path() / ("mforce_ui_control_" + name)) {
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
  }
  ~TempFolder() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
  std::string file(const std::string& name) const { return (path / name).string(); }
};

// A sine instrument and a CheckProbe beside it, whose `refuse` makes the
// build fail: a setting the edit accepts (a bool) and the build refuses.
Patch sine_with_probe() {
  Node pf;
  pf.id = "pf";
  pf.type = "PerformNode";
  pf.settings["field"] = 0.0f;
  Node osc;
  osc.id = "osc";
  osc.type = "SineSource";
  osc.wires["frequency"] = {Wire{"pf"}};
  osc.values["amplitude"] = 0.5f;
  Node probe;
  probe.id = "probe";
  probe.type = "CheckProbe";
  Patch p;
  p.graph.nodes = {pf, osc, probe};
  p.graph.output = "osc";
  p.instrument = InstrumentBlock{};
  return p;
}

void register_check_probe() {
  auto& reg = SourceRegistry::instance();
  if (!reg.has("CheckProbe"))
    reg.register_type("CheckProbe", {.family = Family::Utility, .shelf = Shelf::Experimental},
                      [](auto) { return std::make_shared<three_kinds::CheckProbe>(); });
}

}  // namespace

// Test 1: each edit reaches the patch, the unsaved state and the player.
void run_control_edit_tests() {
  register_all_sources();
  Session s, twin;
  open_file(s, kOboe);
  open_file(twin, kOboe);
  REQUIRE(s.status.empty());
  key_down(s, 1, 60);
  key_down(twin, 1, 60);
  CHECK(buffer(s, 1024) == buffer(twin, 1024));
  LivePlayer* first = s.live.player();
  edit_value(s, "Formant1", "cutoffFreq", 1800.0f);
  CHECK(unsaved(s.open) && s.status.empty());
  CHECK(s.live.player() == first);                     // followed, not rebuilt
  CHECK(buffer(s, 1024) != buffer(twin, 1024));          // the sounding note heard it

  edit_value(s, "Ampl_env", "trigger", 0.5f);            // a constant no player follows: a rebuild
  CHECK(s.live.player() != first && s.status.empty());
  LivePlayer* second = s.live.player();
  edit_setting(s, "Formant1", "normalize", 1.0f);        // a setting: a new player
  CHECK(s.live.player() != second && s.status.empty());

  const Patch before = s.open.patch;
  edit_value(s, "nope", "x", 1.0f);
  CHECK(s.status == "'nope' names no node: it has no 'x'");
  CHECK(s.open.patch == before);

  Session same;
  open_file(same, kOboe);
  LivePlayer* p = same.live.player();
  edit_value(same, "Formant1", "cutoffFreq", 1500.0f);
  edit_setting(same, "Formant1", "normalize", 0.0f);
  edit_position(same, "Formant1", same.open.patch.layout.positions.at("Formant1"));
  CHECK(!unsaved(same.open));                               // the same values again
  CHECK(same.live.player() == p);
  pan_canvas(same, Vec2{12, 34});
  CHECK(!unsaved(same.open) && same.open.patch.layout.panning == (Vec2{12, 34}));
  edit_position(same, "Formant1", Vec2{1, 2});
  CHECK(unsaved(same.open) && same.open.patch.layout.positions.at("Formant1") == (Vec2{1, 2}));
  edit_output_position(same, Vec2{3, 4});
  CHECK(same.open.patch.layout.outputPos == (Vec2{3, 4}));
  select_node(same, "Formant1");
  CHECK(same.selected == "Formant1");
}

// Test 3: a setting edit the build refuses keeps the edit in the patch and
// gives silence with the engine's reason: what sounds is the patch on screen,
// or nothing.
void run_control_failed_rebuild_tests() {
  register_check_probe();
  const TempFolder folder("failed_rebuild");
  save_patch(folder.file("probe.json"), sine_with_probe());
  Session s;
  open_file(s, folder.file("probe.json"));
  REQUIRE(s.status.empty());
  REQUIRE(s.live.player() != nullptr);
  key_down(s, 1, 60);
  edit_setting(s, "probe", "refuse", 1.0f);
  CHECK(s.status == "node 'probe' (CheckProbe): probe says no");
  CHECK(s.live.player() == nullptr);
  CHECK(unsaved(s.open) && s.open.patch.graph.nodes[2].settings.at("refuse") == 1.0f);
  (void)buffer(s);                                       // the fade of the one before
  CHECK(!sounds(s, 4));
}

// Test 4: save, save as, title, and every path of the unsaved guard.
void run_control_file_tests() {
  const TempFolder folder("files");
  Session s;
  CHECK(title(s) == "MForce");
  open_file(s, kOboe);
  CHECK(title(s) == "MForce - oboe1.json");
  CHECK(s.recent.paths.front() == kOboe);
  save_file_as(s, folder.file("a.json"));
  CHECK(s.open.path == folder.file("a.json") && !unsaved(s.open));
  edit_position(s, "Formant1", Vec2{5, 5});
  CHECK(title(s) == "MForce - a.json *");
  save_file(s);
  CHECK(!unsaved(s.open) && load_patch(folder.file("a.json")) == s.open.patch);
  edit_position(s, "Formant1", Vec2{6, 6});
  save_file_as(s, folder.file("no_such_folder/b.json"));
  CHECK(!s.status.empty() && unsaved(s.open) && s.open.path == folder.file("a.json"));

  Session fresh;
  remember(fresh.open.history, fresh.open.patch);
  save_file(fresh);
  CHECK(fresh.status == "the patch has no file yet: save it as one");

  Session missing;
  open_file(missing, kOboe);
  LivePlayer* before = missing.live.player();
  const Patch shown = missing.open.patch;
  missing.recent.paths.insert(missing.recent.paths.begin(), "patches/library/no_such_patch.json");
  open_file(missing, "patches/library/no_such_patch.json");
  CHECK(missing.open.path == kOboe && missing.live.player() == before && missing.recent.paths.size() == 1);
  CHECK(missing.open.patch == shown && !unsaved(missing.open));
  CHECK(missing.status.rfind("patches/library/no_such_patch.json", 0) == 0);
}

void run_control_unsaved_tests() {
  const TempFolder folder("unsaved");
  Session s;
  open_file(s, kOboe);
  request_open(s, kSlowRate);                            // nothing unsaved: at once
  CHECK(s.open.path == kSlowRate);
  request_close(s);
  CHECK(s.unsaved.closing);

  open_file(s, kOboe);
  save_file_as(s, folder.file("o.json"));
  edit_position(s, "Formant1", Vec2{7, 7});
  request_open(s, kSlowRate);
  CHECK(s.unsaved.waiting == Unsaved::Waiting::Open && s.open.path == folder.file("o.json"));
  answer_unsaved(s, Answer::Cancel);
  CHECK(s.unsaved.waiting == Unsaved::Waiting::Nothing && unsaved(s.open));
  request_open(s, kSlowRate);
  answer_unsaved(s, Answer::Discard);
  CHECK(s.open.path == kSlowRate && !unsaved(s.open));

  open_file(s, folder.file("o.json"));
  edit_position(s, "Formant1", Vec2{8, 8});
  request_close(s);
  CHECK(s.unsaved.waiting == Unsaved::Waiting::Close && !s.unsaved.closing);
  answer_unsaved(s, Answer::Save);
  CHECK(s.unsaved.closing && !unsaved(s.open) && load_patch(folder.file("o.json")) == s.open.patch);

  s.unsaved = Unsaved{};
  edit_position(s, "Formant1", Vec2{9, 9});
  s.open.path = folder.file("no_such_folder/o.json");    // a save that fails
  request_close(s);
  answer_unsaved(s, Answer::Save);
  CHECK(s.unsaved.waiting == Unsaved::Waiting::Close && !s.unsaved.closing && !s.status.empty());
  s.open.path.clear();                                   // no file: the wait goes on
  answer_unsaved(s, Answer::Save);
  CHECK(s.unsaved.waiting == Unsaved::Waiting::Close && s.status == "the patch has no file yet: save it as one");
  save_file_as(s, folder.file("n.json"));                // saved under a new name, then Save
  answer_unsaved(s, Answer::Save);
  CHECK(s.unsaved.closing && s.unsaved.waiting == Unsaved::Waiting::Nothing);

  s.unsaved = Unsaved{};
  edit_position(s, "Formant1", Vec2{10, 10});
  request_close(s);
  answer_unsaved(s, Answer::Discard);
  CHECK(s.unsaved.closing);
  s.unsaved = Unsaved{};
  answer_unsaved(s, Answer::Save);                       // nothing waits: nothing happens
  CHECK(!s.unsaved.closing);
}

// Test 6: the player is at the adapter's rate, the patch on screen at its own.
void run_control_rate_tests() {
  Session s;
  open_file(s, kSlowRate);
  REQUIRE(s.open.patch.instrument.has_value());
  REQUIRE(s.live.player() != nullptr);
  CHECK(s.open.patch.sampleRate == 22050);
  CHECK(s.live.player()->sample_rate() == 48000);
  CHECK(s.status == "the file is at 22050 Hz and plays here at 48000 Hz, the output's rate; a render uses the "
                    "file's 22050 Hz");
}

// A refusal shows until the next edit that changes something; an edit that
// changes nothing leaves it.
void run_control_status_tests() {
  Session s;
  open_file(s, kOboe);
  edit_value(s, "nope", "x", 1.0f);
  CHECK(!s.status.empty());
  edit_position(s, "Formant1", s.open.patch.layout.positions.at("Formant1"));   // the same: nothing changes
  CHECK(!s.status.empty());
  edit_position(s, "Formant1", Vec2{11, 12});
  CHECK(s.status.empty());
  edit_value(s, "nope", "x", 1.0f);
  edit_value(s, "Formant1", "cutoffFreq", 1700.0f);
  CHECK(s.status.empty());
}

// edit_string: a name gate's name applies and rebuilds the player; the same
// text again is no change; a name the node does not have is refused.
void run_control_string_tests() {
  const TempFolder folder("strings");
  Patch p = sine_with_probe();
  Node gate;
  gate.id = "g";
  gate.type = "NameGate";
  p.graph.nodes.push_back(gate);
  save_patch(folder.file("gate.json"), p);
  Session s;
  open_file(s, folder.file("gate.json"));
  REQUIRE(s.live.player() != nullptr);
  LivePlayer* before = s.live.player();
  edit_string(s, "g", "name", "attack");
  CHECK(unsaved(s.open) && s.open.patch.graph.nodes[3].strings.at("name") == "attack");
  CHECK(s.live.player() != before);
  save_file(s);
  LivePlayer* saved = s.live.player();
  edit_string(s, "g", "name", "attack");
  CHECK(!unsaved(s.open) && s.live.player() == saved);
  edit_string(s, "g", "nope", "x");
  CHECK(s.status == "node 'g' (NameGate): 'nope' is not a string setting of NameGate");
}

// arrange: an unsaved change that clears the status line and leaves the
// player as it is.
void run_control_arrange_tests() {
  Session s;
  open_file(s, "patches/library/keys/acoustic_piano/piano_default.json");
  LivePlayer* player = s.live.player();
  edit_value(s, "nope", "x", 1.0f);
  REQUIRE(!s.status.empty());
  arrange(s);
  CHECK(unsaved(s.open) && s.status.empty());
  CHECK(s.live.player() == player);
  CHECK(!s.open.patch.layout.panning.has_value());
}

// opened counts the files opened: a failed open does not count.
void run_control_opened_tests() {
  Session s;
  CHECK(s.opened == 0);
  open_file(s, kOboe);
  CHECK(s.opened == 1);
  open_file(s, "patches/library/no_such_patch.json");
  CHECK(s.opened == 1);
  open_file(s, kSlowRate);
  CHECK(s.opened == 2);
}

// keep_up: a value the queue could not take reaches the sounding voice after
// keep_up and a buffer, with no key or edit between; without it, it waits.
void run_control_keep_up_tests() {
  register_check_probe();
  const TempFolder folder("keep_up");
  save_patch(folder.file("sine.json"), sine_with_probe());
  Session told, untold;
  for (Session* s : {&told, &untold}) {
    open_file(*s, folder.file("sine.json"));
    s->keyboard.seconds = 4.0f;
    key_down(*s, 1, 60);
    for (int k = 0; k < 400; ++k) edit_value(*s, "osc", "amplitude", 0.2f + 0.001f * float(k));
    edit_value(*s, "osc", "amplitude", 0.0f);           // the last value: silence
    (void)buffer(*s);                                     // the queue drains; the last value waits
  }
  keep_up(told);
  const auto heard = buffer(told), waited = buffer(untold);
  CHECK(std::all_of(heard.begin(), heard.end(), [](float x) { return x == 0.0f; }));
  CHECK(std::any_of(waited.begin(), waited.end(), [](float x) { return x != 0.0f; }));
}
