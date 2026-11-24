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

// The watchdog's own restart, and its words.
void reopen(Session& s) {
  const std::string died = s.audio.error();
  const std::string failure = s.audio.restart();
  const std::string attempt = "(attempt " + std::to_string(s.watchdog.attempts) + ")";
  if (failure.empty())
    follow_restart(s, "Audio stream reopened " + attempt + " on '" + s.audio.device_name() + "'" +
                          (died.empty() ? std::string() : " — died: " + died));
  else
    follow_restart(s, "Audio reopen FAILED " + attempt + " — " + failure + " — retrying...");
}

}  // namespace

Watch watch(Watchdog& w, std::uint64_t beat, double now) {
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

void watch_audio(Session& s, double now, bool focused) {
  if (!s.audio.asked_to_open()) return;
  if (came_back(s.away, focused, now) && (s.audio.default_device_changed() || !s.audio.running()))
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
  restarted(s, why, s.audio.restart());
}

void restarted(Session& s, std::string_view why, const std::string& failure) {
  const std::string reason(why);
  if (failure.empty()) s.watchdog.attempts = 0;
  follow_restart(s, failure.empty() ? "Audio stream restarted (" + reason + ")"
                                    : "Audio restart FAILED (" + reason + ") — " + failure + " — watchdog will retry");
}

void follow_restart(Session& s, const std::string& words) {
  stop(s);
  s.live.silence();                                // the keys' player was made for the rate before
  sound_follows(s);
  s.status = words;
}

}  // namespace mforce::ui
