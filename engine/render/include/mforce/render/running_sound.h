#pragma once
#include "mforce/patch/patch.h"
#include "mforce/render/mixer.h"
#include <memory>
#include <string>
#include <vector>

namespace mforce {

// The patch with every Envelope held: its time kept in seconds, and a last
// stage that holds its final level appended when it has no stage that holds
// (a stage of 0 percent). Nothing else changes.
Patch held_patch(const Patch& patch);

// A patch's sound running until it is let go: held_patch, built as
// build_sound builds it at `sampleRate`, started once over two hours. A
// patch with an instrument runs its instrument with nothing played: silence.
class RunningSound {
public:
  // How long a running sound is started for.
  static constexpr int kHorizonSeconds = 7200;
  // The most frames a fill renders in one piece; a longer fill is rendered
  // in pieces.
  static constexpr int kBlock = 512;

  // Throws what build_sound throws.
  RunningSound(const Patch& patch, int sampleRate);
  int sample_rate() const { return sampleRate_; }

  // The next `frames` stereo frames, interleaved, WRITTEN to `stereo`.
  // Never blocks, allocates, frees, prints or throws.
  void fill(float* stereo, int frames) noexcept;
  // What the build had to say.
  const std::vector<std::string>& warnings() const { return warnings_; }

private:
  int sampleRate_{0};
  std::unique_ptr<Mixer> mixer_;
  std::vector<std::string> warnings_;
};

}  // namespace mforce
