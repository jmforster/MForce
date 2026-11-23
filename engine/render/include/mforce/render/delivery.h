#pragma once

namespace mforce {

// How a note reaches a pitched instrument's voices.
enum class Delivery {
  Continue,   // the held line is open: the note continues it on its voice
  OpenLine,   // a fresh held note on a voice that can hold: a line opens
  Classic,    // a fresh note, held or not, released inside its own length
};

// Whether a note continues is derived from the instrument's state, never
// declared: an open line takes the note. A fresh note asking to be held
// opens a line only on a voice that can hold one.
inline Delivery choose_delivery(bool lineOpen, bool hold, bool canHold) {
  if (lineOpen) return Delivery::Continue;
  return hold && canHold ? Delivery::OpenLine : Delivery::Classic;
}

} // namespace mforce
