#pragma once

namespace mforce {

// Ambient render state propagated top-down through ValueSource::prepare.
// Intentionally minimal; add fields only when a consumer needs one.
struct RenderContext {
    int sampleRate;
    // Frame index of note-off within this prepare window, or -1 when unknown
    // (continuous streams, plain graph renders). Lets note-off-aware sources
    // (KSPianoString releaseFb damper) act at the right sample. Set by
    // PitchedInstrument::play_note / prepare_voice to the scored duration.
    int noteOffFrame{-1};
};

} // namespace mforce
