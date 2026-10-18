#pragma once

namespace mforce {

// The host's facts, handed to a node once through ValueSource::prepare(ctx):
// the only way a node learns the sample rate. One field today; a host's
// other facts (a block size, say) join here rather than on every prepare
// signature. A default-constructed context (rate 0) means "no host has
// prepared this node yet".
struct RenderContext {
    int sampleRate{0};
};

} // namespace mforce
