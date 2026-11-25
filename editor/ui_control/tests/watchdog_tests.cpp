// The watchdog on a clock of numbers, the focus coming back, a session whose
// adapter was never asked to open, and what follows a restart. No test opens
// the sound card.
#include "mforce/testing/check.h"
#include "transport_fixture.h"
#include "mforce/ui_control/watchdog.h"
#include <cstdio>
#include <string>

using namespace transport_fixture;

// Alive; dead at one second; one attempt each two seconds; still trying after
// ten failures; quiet, its count back at nothing, when a beat arrives.
void run_watch_tests() {
  std::printf("watchdog: alive, dead at a second, an attempt each two seconds for ever, quiet when it beats\n");
  Watchdog w;
  std::uint64_t beat = 0;
  for (int k = 0; k <= 100; ++k) CHECK(watch(w, ++beat, k / 10.0) == Watch::Nothing);
  const double last = 10.0;                                                 // the last beat's time
  CHECK(watch(w, beat, last + 0.75) == Watch::Nothing);
  CHECK(watch(w, beat, last + 1.0) == Watch::Restart && w.attempts == 1);
  CHECK(watch(w, beat, last + 2.75) == Watch::Nothing);
  CHECK(watch(w, beat, last + 3.0) == Watch::Restart && w.attempts == 2);
  int restarts = 2;
  for (double t = last + 3.25; t <= last + 30.0; t += 0.25)
    if (watch(w, beat, t) == Watch::Restart) ++restarts;
  CHECK(restarts == 15 && w.attempts == restarts);                          // still trying after ten failures
  CHECK(watch(w, ++beat, last + 30.25) == Watch::Alive && w.attempts == 0);
  CHECK(watch(w, ++beat, last + 30.5) == Watch::Nothing);
  CHECK(watch(w, beat, last + 31.25) == Watch::Nothing);
}

void run_away_tests() {
  std::printf("watchdog: the focus back after less than two seconds away, and after more\n");
  Away a;
  CHECK(!came_back(a, true, 0.0));
  CHECK(!came_back(a, false, 1.0));
  CHECK(!came_back(a, false, 2.5));
  CHECK(!came_back(a, true, 2.9));                                          // 1.9 s away
  CHECK(!came_back(a, false, 3.0));
  CHECK(came_back(a, true, 5.0));                                           // 2 s away
  CHECK(!came_back(a, true, 6.0));
}

// Ten seconds of clock, the focus going and coming back: nothing happens to a
// session whose adapter was never asked to open, and a restart says so.
void run_watch_unopened_tests() {
  std::printf("watchdog: nothing on an adapter never asked to open\n");
  Session s;
  open_file(s, kOboe);
  const LivePlayer* player = s.live.player();
  const std::string status = s.status;
  for (int k = 0; k < 600; ++k) watch_audio(s, k / 60.0, k < 100 || k > 400);
  CHECK(s.status == status && s.live.player() == player && s.watchdog.attempts == 0);
  restart_audio(s, "by hand");
  CHECK(s.status == "Audio not restarted (by hand): no sound device was ever opened" && s.live.player() == player);
}

// What follows a restart, driven with no device: what was playing stops; at
// the same rate the keys' player is kept and a key still sounds; at another
// rate it is built again; status reads the words.
void run_follow_restart_tests() {
  std::printf("watchdog: after a restart what played stops; the keys' player is kept unless the rate changed\n");
  Session s;
  open_file(s, kOboe);
  play(s);
  key_down(s, 1, 60);
  (void)run(s, 2);                                                          // the output holds both: neither address is free again
  const LivePlayer* before = s.live.player();
  REQUIRE(s.audio.playing(AudioAdapter::Line::Playback) != nullptr && before != nullptr);
  follow_restart(s, "Audio stream restarted (by hand)", s.audio.sample_rate());
  CHECK(s.audio.playing(AudioAdapter::Line::Playback) == nullptr && s.live.player() == before);
  CHECK(s.status == "Audio stream restarted (by hand)");
  key_up(s, 1);
  (void)run(s, 200);
  key_down(s, 2, 60);
  CHECK(run(s, 20) > 0.0f);                                                 // the kept player still plays a key
  key_up(s, 2);
  follow_restart(s, "Audio stream restarted (by hand)", 44100);             // the rate before was another
  CHECK(s.live.player() != nullptr && s.live.player() != before && held_notes(s).empty());
  open_file(s, kPlain);
  play(s);
  follow_restart(s, "Audio stream restarted (back at the window)", s.audio.sample_rate());
  CHECK(s.audio.playing(AudioAdapter::Line::Playback) == nullptr && s.runningPlain == nullptr);
  CHECK(s.status == "Audio stream restarted (back at the window)");
}

// Coming back to the window: a restart for a changed default output, a
// stream not running, or nothing sounding; none while something sounds.
void run_back_reason_tests() {
  std::printf("watchdog: coming back restarts for a new device, a stopped stream or silence; never over a sound\n");
  Away a;
  CHECK(!came_back(a, false, 1.0) && came_back(a, true, 3.5));             // away 2.5 s: the reason is asked
  CHECK(back_reason(true, true, true) == BackReason::DeviceChanged);
  CHECK(back_reason(false, false, true) == BackReason::NotRunning);
  CHECK(back_reason(false, true, false) == BackReason::Silent);
  CHECK(back_reason(false, true, true) == BackReason::None);               // something sounds: left alone
  Session s;
  open_file(s, kOboe);
  play(s);
  CHECK(back_reason(false, true, playing(s)) == BackReason::None);
  stop(s);
  (void)run(s, 50);
  keep_up(s);
  CHECK(back_reason(false, true, playing(s)) == BackReason::Silent);
}

// A restart by hand that opened the device starts the watchdog's count again,
// so its next beat says nothing; one that failed leaves the count to the watchdog.
void run_restarted_tests() {
  std::printf("watchdog: a restart by hand that worked starts the count again: no ALIVE line after it\n");
  Session s;
  open_file(s, kOboe);
  s.watchdog.attempts = 3;
  restarted(s, "by hand", "", s.audio.sample_rate());
  CHECK(s.status == "Audio stream restarted (by hand)" && s.watchdog.attempts == 0);
  CHECK(watch(s.watchdog, s.watchdog.lastBeat + 1, 100.0) == Watch::Nothing);
  s.watchdog.attempts = 2;
  restarted(s, "back at the window", "the sound device did not open: no", s.audio.sample_rate());
  CHECK(s.status == "Audio restart FAILED (back at the window) — the sound device did not open: no — watchdog will retry");
  CHECK(s.watchdog.attempts == 2);
}

// After a restart that opened a stream the watch counts from that moment: at
// a moment the old times would have answered Restart it answers Nothing, for
// a second before the stream is called dead and two before the next try.
void run_restart_resets_watch_tests() {
  std::printf("watchdog: a restart by hand starts the watch's times afresh\n");
  Session s;
  open_file(s, kOboe);
  s.watchdog = Watchdog{5, 0.0, -1e9, 0};
  Watchdog old = s.watchdog;
  CHECK(watch(old, 5, 10.0) == Watch::Restart);                            // the old times: dead
  restarted(s, "by hand", "", s.audio.sample_rate());
  CHECK(watch(s.watchdog, 5, 10.0) == Watch::Nothing);
  CHECK(watch(s.watchdog, 5, 10.99) == Watch::Nothing);
  CHECK(watch(s.watchdog, 5, 11.5) == Watch::Nothing);                     // dead, but the try two seconds off
  CHECK(watch(s.watchdog, 5, 12.0) == Watch::Restart);
}

// The watchdog's try that fails stops nothing, rebuilds nothing, and says
// so once for each new reason; one that opens a stream is followed.
void run_reopen_failed_quiet_tests() {
  std::printf("watchdog: a failed try changes nothing and says each reason once\n");
  Session s;
  open_file(s, kOboe);
  play(s);
  const Sound* playing = s.audio.playing(AudioAdapter::Line::Playback);
  const LivePlayer* player = s.live.player();
  REQUIRE(playing != nullptr && player != nullptr);
  double now = 10.0;
  s.watchdog = Watchdog{1, 0.0, -1e9, 0};
  REQUIRE(watch(s.watchdog, 1, now) == Watch::Restart);
  reopened(s, "", "no device", 48000);
  CHECK(s.status == "Audio reopen FAILED (attempt 1) — no device — retrying...");
  CHECK(s.audio.playing(AudioAdapter::Line::Playback) == playing && s.live.player() == player);
  s.status = "Generated note";
  for (int k = 0; k < 5; ++k, now += 2.0) {
    REQUIRE(watch(s.watchdog, 1, now + 2.0) == Watch::Restart);
    reopened(s, "", "no device", 48000);
  }
  CHECK(s.status == "Generated note");                                      // the same reason is not said again
  reopened(s, "", "the driver failed", 48000);
  CHECK(s.status == "Audio reopen FAILED (attempt 6) — the driver failed — retrying...");
  reopened(s, "", "", 48000);
  CHECK(s.status == "Audio stream reopened (attempt 6) on ''" && s.watchdog.lastFailure.empty());
  CHECK(s.audio.playing(AudioAdapter::Line::Playback) == nullptr && s.live.player() != nullptr);
}
