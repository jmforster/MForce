#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/source/output/sound_channel.h"

namespace mforce {

// ---------------------------------------------------------------------------
// StereoMixer: a plain sound's mix — its channels in order and the two output
// gains. A declaration the plain-sound build reads: the build assembles the
// mixer that plays the sound from these pins, so the part computes nothing.
// Its pins are Manual because no sweep runs on a part.
// ---------------------------------------------------------------------------
struct StereoMixer final : Component {
  Slot<SoundChannel> channels{this, "channels", "a SoundChannel", true};
  Param gainL{this, "gainL", 1.0f, 0.0f, 4.0f, nullptr, Pull::Manual};
  Param gainR{this, "gainR", 1.0f, 0.0f, 4.0f, nullptr, Pull::Manual};

  const char* type_name() const override { return "StereoMixer"; }
};

} // namespace mforce
