#pragma once

namespace mforce {

// Ambient render state propagated top-down through ValueSource::prepare.
// Intentionally minimal; add fields only when a consumer needs one.
struct RenderContext {
    int sampleRate;
};

} // namespace mforce
