#include "mforce/render/node_capture.h"
#include "mforce/render/voice_pool.h"

namespace mforce {

void NodeCapture::begin(const VoicePool& pool, const std::vector<std::string>& ids, int timelineFrames) {
  buffers_.assign(ids.size(), {});
  for (auto& b : buffers_) b.assign(size_t(timelineFrames), 0.0f);
  perVoice_.assign(size_t(pool.size()), {});
  for (int v = 0; v < pool.size(); ++v)
    for (size_t k = 0; k < ids.size(); ++k)
      for (const auto& s : pool.voice(v).signals())
        if (s.id == ids[k]) { perVoice_[size_t(v)].push_back({s.node, int(k)}); break; }
}

} // namespace mforce
