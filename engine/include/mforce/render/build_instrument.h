#pragma once
#include <memory>
#include <string>
#include <vector>
#include "mforce/patch/patch.h"
#include "mforce/render/instrument.h"

namespace mforce {

// The instrument a patch's block describes: the voice pool, each voice its own
// build of the graph. minPolyphony floors the pool's size. A non-null
// `warnings` receives what the build had to say, once.
std::unique_ptr<PitchedInstrument> build_instrument(const Patch& patch, int minPolyphony = 0,
                                                    std::vector<std::string>* warnings = nullptr);

} // namespace mforce
