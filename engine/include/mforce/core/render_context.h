#pragma once

namespace mforce {

using Tick = long long;

// The driver's sample counter. One per pull loop — a voice, a drum source, a
// mixer, a loader adapter, the UI's editor stream, a test host — ticked once
// per sample before that loop's pulls. A node reads it through its context
// and computes once per tick (ValueSource::next()).
struct SampleClock {
    Tick now{0};
    void tick() { ++now; }
};

// The host's facts, handed to a node once through ValueSource::prepare(ctx):
// the only way a node learns the sample rate and the clock it is pulled by.
// A host's other facts (a block size, say) join here rather than on every
// prepare signature. A default-constructed context (rate 0) means "no host
// has prepared this node yet"; a null clock means "no host clock", and every
// pull then computes.
struct RenderContext {
    int sampleRate{0};
    const SampleClock* clock{nullptr};

    // The same facts, driven by the given clock: what a pull loop makes of
    // the context it was handed before it prepares its graph.
    RenderContext with_clock(const SampleClock* c) const {
        RenderContext r = *this;
        r.clock = c;
        return r;
    }
};

} // namespace mforce
