#include "mforce/ui_control/watchdog.h"
#include <string>
#include "mforce/ui_control/generating.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/session.h"

namespace mforce::ui {

namespace {

constexpr double kDeadAfter = 1.0;       // seconds with no beat
constexpr double kRetryEvery = 2.0;      // seconds between restarts
constexpr double kAwayLongEnough = 2.0;  // seconds away from the window

// The watchdog's own restart.
void reopen(Session& s) {
  const std::string died = s.audio.error();
  const int rate = s.audio.sample_rate();
  reopened(s, died, s.audio.restart(), rate);
}

}  // namespace

Watch watch(Watchdog& w, std::uint64_t beat, double now) {
  if (w.fresh) {
    w.fresh = false;
    w.lastBeat = beat;
    w.lastChange = now;
    w.lastAttempt = now;
    return Watch::Nothing;
  }
  if (beat != w.lastBeat) {
    w.lastBeat = beat;
    w.lastChange = now;
    const bool back = w.attempts > 0;
    w.attempts = 0;
    return back ? Watch::Alive : Watch::Nothing;
  }
  if (now - w.lastChange < kDeadAfter || now - w.lastAttempt < kRetryEvery) return Watch::Nothing;
  w.lastAttempt = now;
  ++w.attempts;
  return Watch::Restart;
}

bool came_back(Away& a, bool focused, double now) {
  const bool back = focused && !a.focused && now - a.since >= kAwayLongEnough;
  if (a.focused && !focused) a.since = now;
  a.focused = focused;
  return back;
}

BackReason back_reason(bool deviceChanged, bool running, bool sounding) {
  if (deviceChanged) return BackReason::DeviceChanged;
  if (!running) return BackReason::NotRunning;
  if (!sounding) return BackReason::Silent;
  return BackReason::None;
}

void watch_audio(Session& s, double now, bool focused) {
  if (!s.audio.asked_to_open()) return;
  if (came_back(s.away, focused, now) &&
      back_reason(s.audio.default_device_changed(), s.audio.running(), playing(s)) != BackReason::None)
    restart_audio(s, "back at the window");
  switch (watch(s.watchdog, s.audio.heartbeat(), now)) {
    case Watch::Restart: reopen(s); break;
    case Watch::Alive:   s.status = "Audio callback ALIVE again (post-reopen)"; break;
    case Watch::Nothing: break;
  }
}

void restart_audio(Session& s, std::string_view why) {
  const std::string reason(why);
  if (!s.audio.asked_to_open()) {
    s.status = "Audio not restarted (" + reason + "): no sound device was ever opened";
    return;
  }
  const int rate = s.audio.sample_rate();
  restarted(s, why, s.audio.restart(), rate);
}

void restarted(Session& s, std::string_view why, const std::string& failure, int rateBefore) {
  const std::string reason(why);
  Watchdog& w = s.watchdog;
  if (!failure.empty()) {
    w.lastFailure = failure;
    s.status = "Audio restart FAILED (" + reason + ") — " + failure + " — watchdog will retry";
    return;
  }
  w.attempts = 0;
  w.fresh = true;
  w.lastFailure.clear();
  follow_restart(s, "Audio stream restarted (" + reason + ")", rateBefore);
}

void reopened(Session& s, const std::string& died, const std::string& failure, int rateBefore) {
  Watchdog& w = s.watchdog;
  const std::string attempt = "(attempt " + std::to_string(w.attempts) + ")";
  if (failure.empty()) {
    w.fresh = true;
    w.lastFailure.clear();
    follow_restart(s, "Audio stream reopened " + attempt + " on '" + s.audio.device_name() + "'" +
                          (died.empty() ? std::string() : " — died: " + died),
                   rateBefore);
    return;
  }
  if (failure == w.lastFailure) return;
  w.lastFailure = failure;
  s.status = "Audio reopen FAILED " + attempt + " — " + failure + " — retrying...";
}

void follow_restart(Session& s, const std::string& words, int rateBefore) {
  stop(s);
  if (s.audio.sample_rate() != rateBefore) {      // the keys' player was made for the rate before
    s.live.silence();
    sound_follows(s);
  }
  s.status = words;
}

}  // namespace mforce::ui
