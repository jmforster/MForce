#include "mforce/render/build_instrument.h"
#include <algorithm>
#include <stdexcept>

namespace mforce {

std::unique_ptr<PitchedInstrument> build_instrument(const Patch& patch, int minPolyphony,
                                                    std::vector<std::string>* warnings) {
  if (!patch.instrument) throw std::runtime_error("Patch has no 'instrument' section");
  const InstrumentBlock& block = *patch.instrument;
  auto state = std::make_shared<InstrumentState>();
  VoicePool pool;
  const int polyphony = std::max(block.polyphony, minPolyphony);
  for (int v = 0; v < polyphony; ++v)
    pool.add(Voice(patch.graph, block, state, v == 0 ? warnings : nullptr));
  auto inst = std::make_unique<PitchedInstrument>(InstrumentClass{block.sustaining, block.onsets},
                                                  block.glideMs / 1000.0f, std::move(pool), std::move(state));
  inst->volume = block.volume;
  inst->prepare(RenderContext{patch.sampleRate});
  return inst;
}

} // namespace mforce
