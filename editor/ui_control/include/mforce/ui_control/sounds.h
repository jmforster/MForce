#pragma once
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include "mforce/render/live_player.h"
#include "mforce/render/running_sound.h"
#include "mforce/ui_audio/sound.h"
#include "mforce/ui_control/take.h"

namespace mforce::ui {

// The three things the output plays. Each is made on the control thread and
// filled on the audio thread (`final`: no class derives from it further,
// C#'s `sealed`).

// The keys' instrument: a live player, its mono output on both channels.
class KeysSound final : public Sound {
public:
  explicit KeysSound(std::unique_ptr<LivePlayer> player);
  // For the control thread's note_on, note_off, set_value and the rest.
  LivePlayer& player() { return *player_; }
  void fill(float* stereo, int frames) noexcept override;

private:
  std::unique_ptr<LivePlayer> player_;
  std::array<float, 512> mono_{};
};

// A take played once from its start, at the output's rate: when the take's
// rate is another, its frames are made again at `outputRate` here, by
// linear interpolation, so the take lasts the same seconds. Over when its
// last frame has played.
class TakeSound final : public Sound {
public:
  TakeSound(const Take& take, int outputRate);
  void fill(float* stereo, int frames) noexcept override;
  bool over() const noexcept override { return over_.load(std::memory_order_acquire); }
  int  frames() const { return int(stereo_.size() / 2); }

private:
  std::vector<float> stereo_;
  size_t at_{0};                                   // audio thread only: the next frame
  std::atomic<bool> over_{false};
};

// A plain sound running until it is stopped: the engine's RunningSound.
class PlainSound final : public Sound {
public:
  PlainSound(const Patch& patch, int sampleRate);  // throws what the build throws
  void fill(float* stereo, int frames) noexcept override;
  const std::vector<std::string>& warnings() const { return running_.warnings(); }

private:
  RunningSound running_;
};

}  // namespace mforce::ui
