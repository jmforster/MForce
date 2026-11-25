// Listening: the keys hear a node mid-chain at volume 1; Generate never
// does; what ends it and what it follows; the refusals in their readers'
// words with the keys' player untouched; a running plain sound follows it.
#include "mforce/ui_control/selecting.h"
#include "mforce/testing/check.h"
#include "transport_fixture.h"
#include "control_fixture.h"
#include "mforce/ui_control/building.h"
#include "mforce/ui_control/listening.h"
#include <cstdio>
#include <string>
#include <vector>

using namespace transport_fixture;

void run_listen_tests() {
  std::printf("listen: the keys hear the node at volume 1; no words; Generate never hears it\n");
  Session s;
  open_file(s, kOboe);
  const LivePlayer* before = s.live.player();
  const std::string status = s.status;
  const size_t steps = s.open.history.past.size();
  listen_at(s, "Damp_lpf");
  CHECK(s.listening == "Damp_lpf" && s.live.player() != nullptr && s.live.player() != before);
  CHECK(s.status == status && s.open.history.past.size() == steps && !unsaved(s.open));
  const Patch heard = heard_patch(s);
  CHECK(heard.graph.output == "Damp_lpf" && heard.instrument->volume == 1.0f);
  CHECK(s.open.patch.graph.output == "Reverb" && s.open.patch.instrument->volume == 0.5f);
  key_down(s, 1, 60);
  CHECK(run(s, 20) > 0.0f);
  key_up(s, 1);
  const LivePlayer* listening = s.live.player();
  listen_at(s, "Damp_lpf");
  CHECK(s.live.player() == listening);                                      // the node already listened to: nothing
  generate(s);
  const std::vector<float> tapped = s.take.stereo;
  stop_listening(s);
  CHECK(s.listening.empty() && heard_patch(s) == s.open.patch && s.live.player() != listening);
  generate(s);
  CHECK(!tapped.empty() && s.take.stereo == tapped);                        // sample for sample
}

void run_listen_ends_tests() {
  std::printf("listen: a delete, an undo or redo that takes or renames it, New and Open end it; a rename follows\n");
  Session s;
  open_file(s, kOboe);
  listen_at(s, "Damp_lpf");
  rename(s, "Damp_lpf", "Damp2");
  CHECK(s.listening == "Damp2" && heard_patch(s).graph.output == "Damp2");
  undo(s);                                                                  // the rename undone: its name is gone
  CHECK(s.listening.empty());
  redo(s);
  listen_at(s, "Damp2");
  delete_these(s, {}, {"Damp2"});
  CHECK(s.listening.empty() && s.live.player() != nullptr);
  create_node(s, "SineSource", Vec2{0, 0});
  const std::string made = selected_one(s);
  listen_at(s, made);
  CHECK(s.listening == made);
  undo(s);
  CHECK(s.listening.empty());
  redo(s);
  listen_at(s, made);
  redo(s);                                                                  // nothing to redo: still listening
  CHECK(s.listening == made);
  new_file(s);
  CHECK(s.listening.empty());
  open_file(s, kOboe);
  listen_at(s, "Formant1");
  open_file(s, kOboe);
  CHECK(s.listening.empty());
}

void run_listen_refusal_tests() {
  std::printf("listen: a face, the Output and a part refused in words, the keys' player untouched\n");
  Session s;
  Patch p = control_fixture::control_patch();
  (void)add_node(p, "SoundChannel", "ch", 9, Vec2{0, 400});
  (void)add_node(p, "StereoMixer", "mix", 10, Vec2{0, 500});
  control_fixture::start(s, p);
  const LivePlayer* before = s.live.player();
  REQUIRE(before != nullptr);
  const auto refused = [&](const std::string& node, const std::string& words) {
    s.status.clear();
    listen_at(s, node);
    const bool ok = s.listening.empty() && s.live.player() == before && s.status.find(words) != std::string::npos;
    if (!ok) std::printf("  listen_at(%s): %s\n", node.c_str(), s.status.c_str());
    return ok;
  };
  CHECK(refused("Note1", "'Note1' is a Note"));
  CHECK(refused("", "the Output node is what the keys play already"));
  CHECK(refused("ch", "is a SoundChannel, a part, not a signal"));          // check()'s words
  CHECK(refused("mix", "is a StereoMixer, a part, not a signal"));          // the voice build's words
  CHECK(refused("nobody", "names no node"));
}

void run_listen_plain_tests() {
  std::printf("listen: a running plain sound follows listening, and is left running when the build refuses\n");
  Session s;
  open_file(s, kPlain);
  play(s);
  const Sound* first = s.audio.playing(AudioAdapter::Line::Playback);
  REQUIRE(first != nullptr);
  keep_up(s);
  listen_at(s, "fm");
  const Sound* second = s.audio.playing(AudioAdapter::Line::Playback);
  CHECK(s.listening == "fm" && second != nullptr && second != first && run(s, 10) > 0.0f);
  keep_up(s);
  listen_at(s, "ch1");
  CHECK(s.listening == "fm" && s.audio.playing(AudioAdapter::Line::Playback) == second);
  CHECK(s.status.find("a part, not a signal") != std::string::npos);
  stop_listening(s);
  CHECK(s.audio.playing(AudioAdapter::Line::Playback) != second);
  stop(s);
}

// On a plain sound with nothing running, listen_at still asks the plain
// build: a mixer whose channel has no source passes check() and the plain
// build refuses it, in its words; nothing changes and nothing starts.
void run_listen_plain_build_tests() {
  std::printf("listen: on a plain sound with nothing running, the plain build is asked\n");
  Session s;
  open_file(s, kPlain);
  Patch p = s.open.patch;
  (void)add_node(p, "SoundChannel", "ch2", 12, Vec2{0, 500});
  (void)add_node(p, "StereoMixer", "mix2", 11, Vec2{0, 600});
  (void)connect(p, "mix2", "channels", "ch2", false);
  s.open.patch = p;
  s.status.clear();
  listen_at(s, "mix2");
  CHECK(s.listening.empty() && s.status.find("'source' is not wired") != std::string::npos);
  CHECK(s.audio.playing(AudioAdapter::Line::Playback) == nullptr);
  listen_at(s, "fm");
  CHECK(s.listening == "fm" && s.audio.playing(AudioAdapter::Line::Playback) == nullptr);   // nothing ran: nothing starts
}
