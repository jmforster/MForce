#pragma once
#include "mforce/render/mixer.h"
#include <memory>
#include <string>
#include <vector>

namespace mforce {

// A sound ready to render: its rate, its length in frames and its mixer.
struct BuiltSound {
  int sampleRate{48000};
  int frames{48000 * 5};
  std::unique_ptr<Mixer> mixer;
  std::vector<std::string> warnings;                     // what the build had to say; the host prints it
};

} // namespace mforce
