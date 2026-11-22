#include "mforce/render/build_instrument.h"
#include <algorithm>
#include <stdexcept>

namespace mforce {

std::unique_ptr<PitchedInstrument> build_instrument(const Patch& patch, int minPolyphony,
                                                    std::vector<std::string>* warnings) {
  if (!patch.instrument) throw std::runtime_error("Patch has no 'instrument' section");
  const InstrumentBlock& block = *patch.instrument;
  auto inst = std::make_unique<PitchedInstrument>(InstrumentClass{block.sustaining, block.onsets},
                                                  block.glideMs / 1000.0f);
  inst->volume = block.volume;
  const int polyphony = std::max(block.polyphony, minPolyphony);
  for (int v = 0; v < polyphony; ++v)
    inst->pool().add(Voice(patch.graph, block, inst->instrument_state(), v == 0 ? warnings : nullptr));
  inst->prepare(RenderContext{patch.sampleRate});
  return inst;
}

} // namespace mforce
