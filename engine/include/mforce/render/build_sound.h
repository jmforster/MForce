#pragma once
#include "mforce/patch/patch.h"
#include "mforce/render/built_sound.h"

namespace mforce {

// A patch as a sound to render: an instrument playing its embedded score, a
// mixer over a plain sound's channels, or one channel around a bare output.
BuiltSound build_sound(const Patch& patch);

} // namespace mforce
