#pragma once

namespace mforce::ui {

struct Session;

// The stale banner: shown while the session's build stamp is stale (a source
// file of the program is newer than it) until its Dismiss is clicked, which
// holds for the rest of the session.
bool banner_shown(const Session& s);
void dismiss_banner(Session& s);

}  // namespace mforce::ui
