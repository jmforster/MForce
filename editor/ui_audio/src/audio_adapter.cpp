// The AudioAdapter's players: the hand-over between the two threads, the
// fade of the one replaced, and the fill. The device is in audio_device.cpp.
#include "mforce/ui_audio/audio_adapter.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace mforce::ui {

// --- control thread -------------------------------------------------------

void AudioAdapter::destroy(LivePlayer* p) {
  owned_.erase(std::remove_if(owned_.begin(), owned_.end(),
                              [p](const std::unique_ptr<LivePlayer>& owned) { return owned.get() == p; }),
               owned_.end());
}

void AudioAdapter::reclaim() {
  for (auto& slot : handedBack_)
    if (LivePlayer* p = slot.exchange(nullptr, std::memory_order_acq_rel)) destroy(p);
}

void AudioAdapter::play(std::unique_ptr<LivePlayer> player) {
  reclaim();
  if (player && player->sample_rate() != rate_)
    throw std::invalid_argument("a player at " + std::to_string(player->sample_rate()) +
                                " Hz cannot sound through the output at " + std::to_string(rate_) + " Hz");
  LivePlayer* raw = player.get();
  if (player) owned_.push_back(std::move(player));
  // reinterpret_cast turns the address into a number and back (C#: an
  // IntPtr), so one atomic word carries "nothing", "silence" or a player.
  const std::uintptr_t offer = raw ? reinterpret_cast<std::uintptr_t>(raw) : kSilence;
  const std::uintptr_t unseen = offered_.exchange(offer, std::memory_order_acq_rel);
  if (unseen > kSilence) destroy(reinterpret_cast<LivePlayer*>(unseen));   // never taken by the audio thread
  newest_ = raw;
}

LivePlayer* AudioAdapter::player() {
  reclaim();
  return newest_;
}

float AudioAdapter::peak() const { return peak_.load(std::memory_order_relaxed); }

// --- audio thread ---------------------------------------------------------

// Into an empty hand-back slot; parked here while every slot is full, and
// offered again at the next fill. A swap needs a play() call, which empties
// the slots first, and hands back at most two players (the one cut, and the
// one whose fade ends), so the slots do not fill in practice.
void AudioAdapter::hand_back(LivePlayer* p) noexcept {
  for (auto& slot : handedBack_) {
    LivePlayer* empty = nullptr;
    if (slot.compare_exchange_strong(empty, p, std::memory_order_acq_rel)) return;
  }
  for (auto& parked : parked_)
    if (!parked) {
      parked = p;
      return;
    }
}

// The next player sounds; the one it replaces fades; one still fading from
// an earlier swap is cut and handed back.
void AudioAdapter::take(LivePlayer* next) noexcept {
  if (fading_) hand_back(fading_);
  fading_ = current_;
  fadeLeft_ = fading_ ? fadeSamples_ : 0;
  current_ = next;
}

void AudioAdapter::fill_chunk(float* stereo, int frames, float& peak) noexcept {
  if (current_) current_->fill(mono_.data(), frames);
  else std::fill(mono_.begin(), mono_.begin() + frames, 0.0f);
  if (fading_) {
    fading_->fill(fadingMono_.data(), frames);
    for (int i = 0; i < frames && fadeLeft_ > 0; ++i, --fadeLeft_)
      mono_[size_t(i)] += fadingMono_[size_t(i)] * float(fadeLeft_) / float(fadeSamples_);
    if (fadeLeft_ == 0) {
      hand_back(fading_);
      fading_ = nullptr;
    }
  }
  for (int i = 0; i < frames; ++i) {
    const float s = mono_[size_t(i)];
    stereo[2 * i] = s;
    stereo[2 * i + 1] = s;
    peak = std::max(peak, std::fabs(s));
  }
}

void AudioAdapter::fill(float* stereo, int frames) noexcept {
  for (auto& parked : parked_) {
    LivePlayer* p = parked;
    parked = nullptr;
    if (p) hand_back(p);
  }
  const std::uintptr_t offer = offered_.exchange(kNothing, std::memory_order_acq_rel);
  if (offer != kNothing) take(offer == kSilence ? nullptr : reinterpret_cast<LivePlayer*>(offer));
  float peak = 0.0f;
  for (int done = 0; done < frames;) {
    const int n = std::min(kChunk, frames - done);
    fill_chunk(stereo + 2 * done, n, peak);
    done += n;
  }
  peak_.store(peak, std::memory_order_relaxed);
}

}  // namespace mforce::ui
