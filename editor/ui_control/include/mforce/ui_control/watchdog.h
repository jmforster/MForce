#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace mforce::ui {

struct Session;

// The sound card looked after: when its callback stops the stream is opened
// again, and again until it comes back; coming back to the window after a
// while away opens it again when the system's default output changed or the
// stream stopped. When to act is plain data a test drives with a clock of
// numbers; what a restart does is restart_audio.

// The heartbeat as last seen.
struct Watchdog {
  std::uint64_t lastBeat{0};
  double lastChange{0};        // when the beat last moved
  double lastAttempt{-1e9};    // when a restart was last tried
  int attempts{0};             // restarts tried since the beat last moved
};
enum class Watch { Nothing, Restart, Alive };
// Dead after a second with no new beat; then one restart every two seconds,
// for as long as it takes (Restart). A beat after one or more restarts
// answers Alive once and counts the attempts from nothing again.
Watch watch(Watchdog& w, std::uint64_t beat, double now);

// The window's focus as last seen.
struct Away {
  bool focused{true};
  double since{0};             // when the focus went
};
// True on the call at which the focus comes back after two seconds or more away.
bool came_back(Away& a, bool focused, double now);

// Once a frame, with the window's clock and focus: the watch, and the focus
// coming back (a restart then only when the default output changed or the
// stream is not running). Nothing on an adapter never asked to open.
void watch_audio(Session& s, double now, bool focused);
// The stream closed and opened again, `why` ("by hand", "back at the
// window") in the status; an adapter never asked to open is left alone, and
// the status says so.
void restart_audio(Session& s, std::string_view why);
// What restart_audio does once the adapter has answered (`failure` empty:
// it opened): the words, follow_restart, and on success the watchdog's
// count of attempts started again, so its "ALIVE again" does not follow.
void restarted(Session& s, std::string_view why, const std::string& failure);
// What follows any restart, with or without a device: what was playing
// stops, the keys' player is built again at the device's rate, and status
// reads `words`.
void follow_restart(Session& s, const std::string& words);

}  // namespace mforce::ui
