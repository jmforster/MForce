#pragma once
#include <string_view>
#include "mforce/patch/patch.h"

namespace mforce::ui {

struct Session;

// Listening: the keys, and a plain sound that is running, play one node's
// signal in place of the patch's output. Session::listening names the node;
// it is not saved, not an undo step and not an unsaved change. Generate, the
// take, Save WAV and the spectrum never hear it.

// Listens at `node`: the keys' player, and a plain sound that is running,
// are built again from heard_patch at once. Refused in the reader's own
// words (check() of the copy; for an instrument, the voice build), and by
// name for a note face and for the Output node (an empty name): the
// refusal goes to status and nothing changes. The node already listened to
// changes nothing. No words on success.
void listen_at(Session& s, std::string_view node);
// The patch's own output again; the sound follows.
void stop_listening(Session& s);
// The patch the keys play: the open one, or, while listening, a copy whose
// output is the node listened to and whose instrument volume is 1.
Patch heard_patch(const Session& s);

}  // namespace mforce::ui
