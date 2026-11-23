#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/render/mono_source.h"
#include <memory>

namespace mforce {

// One mixer channel: its mono producer, its volume and its pan in [-1, 1].
struct Channel {
  std::unique_ptr<MonoSource> source;
  std::shared_ptr<ValueSource> volume;
  std::shared_ptr<ValueSource> pan;
};

} // namespace mforce
