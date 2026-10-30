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

// minPolyphony: floor on the voice-pool size (0 = respect the patch's own
// "polyphony" value). Live-keyboard callers pass a floor so patches authored
// with small pools (default 4) still give an 88-key board enough voices.
InstrumentPatch load_instrument_patch(const std::string& path,
                                      int minPolyphony = 0);

// Same as load_instrument_patch, from already-serialized JSON text —
// no file. The UI's Generate path serializes its live editor graph and
// loads it directly (render-capture unification spec 2026-09-13).
InstrumentPatch load_instrument_patch_json(const std::string& jsonText,
                                           int minPolyphony = 0);

} // namespace mforce
