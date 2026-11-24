// The AudioAdapter's two lines and its fill: each line's sound, the sum, the
// clip and the peaks. The hand-over of one line is sound_line.cpp; the
// device is audio_device.cpp.
#include "mforce/ui_audio/audio_adapter.h"
#include <algorithm>
#include <cmath>
#include "mforce/render/limiter.h"

namespace mforce::ui {

// --- control thread -------------------------------------------------------

void AudioAdapter::play(Line l, std::unique_ptr<Sound> sound) { line(l).play(std::move(sound)); }

Sound* AudioAdapter::playing(Line l) { return line(l).playing(); }

std::uint64_t AudioAdapter::heartbeat() const { return beats_.load(std::memory_order_relaxed); }

float AudioAdapter::peak() const { return peak_.load(std::memory_order_relaxed); }

float AudioAdapter::peak_before_clip() const { return peakBeforeClip_.load(std::memory_order_relaxed); }

// --- audio thread ---------------------------------------------------------

// In order: the heartbeat counted; each line takes what was offered to it; then, a chunk at a
// time, each line writes its sound (and the one fading out) into its own
// buffer, the two are summed into the output, the sum's peak is taken, the
// sum goes through soft_clip, and the clipped peak is taken. The peaks of
// the whole buffer are stored last.
void AudioAdapter::fill(float* stereo, int frames) noexcept {
  beats_.fetch_add(1, std::memory_order_relaxed);
  for (SoundLine& l : lines_) l.begin_buffer();
  float before = 0.0f, after = 0.0f;
  for (int done = 0; done < frames;) {
    const int n = std::min(SoundLine::kChunk, frames - done);
    lines_[0].fill_chunk(keys_.data(), n);
    lines_[1].fill_chunk(playback_.data(), n);
    float* out = stereo + 2 * done;
    for (int i = 0; i < 2 * n; ++i) {
      const float sum = keys_[size_t(i)] + playback_[size_t(i)];
      before = std::max(before, std::fabs(sum));
      out[i] = soft_clip(sum);
      after = std::max(after, std::fabs(out[i]));
    }
    done += n;
  }
  peakBeforeClip_.store(before, std::memory_order_relaxed);
  peak_.store(after, std::memory_order_relaxed);
}

}  // namespace mforce::ui
