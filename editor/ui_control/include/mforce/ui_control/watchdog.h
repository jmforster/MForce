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
  bool fresh{false};           // a stream was just opened: the next look starts the times from then
  std::string lastFailure;     // why the last restart failed, as last said; empty after one that worked
};
enum class Watch { Nothing, Restart, Alive };
// Dead after a second with no new beat; then one restart every two seconds,
// for as long as it takes (Restart). A beat after one or more restarts
// answers Alive once and counts the attempts from nothing again. After a
// restart that opened a stream (`fresh`), the next look counts from that
// moment: a second before the stream is dead, two before the next attempt.
Watch watch(Watchdog& w, std::uint64_t beat, double now);

// The window's focus as last seen.
struct Away {
  bool focused{true};
  double since{0};             // when the focus went
};
// True on the call at which the focus comes back after two seconds or more away.
bool came_back(Away& a, bool focused, double now);
// Why coming back to the window restarts the stream: the default output
// changed, the stream is not running, or nothing is sounding (a stream can
// go on ticking with nothing heard after another program took the device,
// which no heartbeat shows). None while something sounds: it is not cut.
enum class BackReason { None, DeviceChanged, NotRunning, Silent };
BackReason back_reason(bool deviceChanged, bool running, bool sounding);

// Once a frame, with the window's clock and focus: the watch, and the focus
// coming back (a restart then for back_reason's reasons). Nothing on an
// adapter never asked to open.
void watch_audio(Session& s, double now, bool focused);
// The stream closed and opened again, `why` ("by hand", "back at the
// window") in the status; an adapter never asked to open is left alone, and
// the status says so.
void restart_audio(Session& s, std::string_view why);
// What restart_audio does once the adapter has answered (`failure` empty:
// it opened; `rateBefore`: the device's rate before the restart). Opened: follow_restart with the words, the watchdog's count of
// attempts started again (no "ALIVE again" follows) and its times started
// afresh. Failed: the words alone; nothing stops and nothing is rebuilt.
void restarted(Session& s, std::string_view why, const std::string& failure, int rateBefore);
// What the watchdog's own restart does once the adapter has answered: opened,
// follow_restart with the reopened words (and " — died: <died>" when the
// device had said why), its times started afresh; failed, nothing stops and
// nothing is rebuilt, and the FAILED words are written only when the reason
// differs from the one last said.
void reopened(Session& s, const std::string& died, const std::string& failure, int rateBefore);
// What follows a restart that opened a stream, with or without a device:
// what was playing stops; the keys' player is built again only when the
// device's rate is not `rateBefore` (at the same rate the one there is kept);
// status reads `words`.
void follow_restart(Session& s, const std::string& words, int rateBefore);

}  // namespace mforce::ui
