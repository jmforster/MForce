#pragma once
#include <memory>
#include <string>
#include "mforce/render/mixer.h"
#include "mforce/render/instrument.h"

namespace mforce {

struct Patch {
  int sampleRate{48000};
  int frames{48000*5};
  std::unique_ptr<StereoMixer> mixer;
};

Patch load_patch_file(const std::string& path);

// Load a PitchedInstrument from a patch JSON (no score, no mixer wiring).
// Returns the instrument + sampleRate for external use (e.g. Conductor).
struct InstrumentPatch {
  std::unique_ptr<PitchedInstrument> instrument;
  int sampleRate{48000};
};

InstrumentPatch load_instrument_patch(const std::string& path);

} // namespace mforce
