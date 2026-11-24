#pragma once
#include <string>
#include <vector>
#include "mforce/ui_control/transport.h"

namespace mforce::ui {

// The last render, kept until the next one or another patch.
struct Take {
  std::vector<float> stereo;       // interleaved
  int sampleRate{0};
  int frames{0};
  std::vector<std::string> stripIds;
  std::vector<std::vector<float>> strips;   // one per id, the node's raw output over the take
  // What it was made from: a Note take is played again only while these
  // still match the Note tab and the keyboard.
  Transport::Mode mode{Transport::Mode::Note};
  std::string note;
  float velocity{0.0f}, seconds{0.0f};
};

}  // namespace mforce::ui
