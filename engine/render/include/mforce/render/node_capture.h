#pragma once
#include "mforce/core/dsp_value_source.h"
#include <string>
#include <vector>

namespace mforce {

class VoicePool;

// Per-node strips during an offline render: for each captured id, a
// timeline-length buffer every voice's clone of that node is summed into at
// the frame it played, as the instrument sums the voices' audio. The strips
// record the node's raw output, with no velocity or volume. The buffers are
// allocated at begin(), so recording allocates nothing.
class NodeCapture {
public:
  // Starts capturing `ids` over `timelineFrames` frames on `pool`'s voices.
  void begin(const VoicePool& pool, const std::vector<std::string>& ids, int timelineFrames);
  // Stops recording; the buffers stay for the caller.
  void end() { perVoice_.clear(); }
  bool active() const { return !perVoice_.empty(); }

  // Voice `voice` played timeline frame `frame`: each of its captured nodes
  // adds its current output there.
  void record(int voice, int frame) {
    for (const auto& e : perVoice_[size_t(voice)]) {
      auto& dst = buffers_[size_t(e.buffer)];
      if (frame >= 0 && frame < int(dst.size()))
        dst[size_t(frame)] += e.node->current();
    }
  }

  // One buffer per captured id, in the order begin() was given.
  const std::vector<std::vector<float>>& buffers() const { return buffers_; }

private:
  struct Entry { ValueSource* node; int buffer; };
  std::vector<std::vector<Entry>> perVoice_;     // parallel to the pool
  std::vector<std::vector<float>> buffers_;
};

} // namespace mforce
