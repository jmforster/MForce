#pragma once
#include "mforce/core/dsp_value_source.h"

namespace mforce {

// ---------------------------------------------------------------------------
// SoundChannel: one channel of a plain sound's mix — a signal, its level and
// its place between left and right. A declaration the plain-sound build
// reads: the build hands `source` to a channel adapter of its own and
// `volume` and `pan` to the mixer that plays them, so the part computes
// nothing. Its pins are Manual because no sweep runs on a part.
// ---------------------------------------------------------------------------
struct SoundChannel final : Component {
  Input source{this, "source", Input::RequiredInput, nullptr, Pull::Manual};
  Param volume{this, "volume", 1.0f, 0.0f, 4.0f, nullptr, Pull::Manual};
  Param pan{this, "pan", 0.0f, -1.0f, 1.0f, nullptr, Pull::Manual};

  const char* type_name() const override { return "SoundChannel"; }
};

} // namespace mforce
