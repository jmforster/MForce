#pragma once

namespace mforce {

// Ambient render state propagated top-down through ValueSource::prepare.
// Intentionally minimal; add fields only when a consumer needs one.
// noteOffFrame was removed 2026-08-13 (note-contained sound): release is
// the final stage of an Envelope inside the note's duration, so no node
// needs to know where the gate ends (KSPianoString's damper is a
// ValueSource input now).
struct RenderContext {
    int sampleRate;
};

} // namespace mforce
