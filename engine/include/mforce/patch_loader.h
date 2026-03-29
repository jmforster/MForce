#pragma once
#include <memory>
#include <string>
#include "mforce/mixer.h"

namespace mforce {

struct Patch {
  int sampleRate{48000};
  int frames{48000*5};
  std::unique_ptr<StereoMixer> mixer;
};

Patch load_patch_file(const std::string& path);

} // namespace mforce
