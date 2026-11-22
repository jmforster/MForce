#pragma once
#include "mforce/patch/patch.h"
#include "mforce/render/built_sound.h"
#include "mforce/render/instrument.h"
#include <memory>

namespace mforce {

// A patch as a sound to render: its instrument, nothing played, in one unity
// channel; a mixer over a plain sound's channels; or one channel around a
// bare output. Its length is the patch's `seconds`, else five seconds.
BuiltSound build_sound(const Patch& patch);

// The patch's length in frames: its `seconds`, else five seconds.
int sound_frames(const Patch& patch);

// A built instrument in one unity channel at centre pan, the mixer prepared
// at `sampleRate` (which prepares the instrument again).
std::unique_ptr<Mixer> instrument_mixer(std::unique_ptr<Instrument> instrument, int sampleRate);

} // namespace mforce
