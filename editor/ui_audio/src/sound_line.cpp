// A line's hand-over between the two threads, the fade of the sound
// replaced, and its fill.
#include "mforce/ui_audio/sound_line.h"
#include <algorithm>

namespace mforce::ui {

// Called only while no callback runs (before the device starts, or with it
// closed), so the audio thread's count of what is left of a fade may be
// written here: kept within the new length, so a fade's gain never passes 1.
void SoundLine::set_fade(int samples) {
  fadeSamples_ = std::max(1, samples);
  fadeLeft_ = std::min(fadeLeft_, fadeSamples_);
}

// --- control thread -------------------------------------------------------

void SoundLine::destroy(Sound* s) {
  owned_.erase(std::remove_if(owned_.begin(), owned_.end(),
                              [s](const std::unique_ptr<Sound>& owned) { return owned.get() == s; }),
               owned_.end());
}

void SoundLine::reclaim() {
  for (auto& slot : handedBack_)
    if (Sound* s = slot.exchange(nullptr, std::memory_order_acq_rel)) destroy(s);
}

void SoundLine::play(std::unique_ptr<Sound> sound) {
  reclaim();
  Sound* raw = sound.get();
  if (sound) owned_.push_back(std::move(sound));
  // reinterpret_cast turns the address into a number and back (C#: an
  // IntPtr), so one atomic word carries "nothing", "silence" or a sound.
  const std::uintptr_t offer = raw ? reinterpret_cast<std::uintptr_t>(raw) : kSilence;
  const std::uintptr_t unseen = offered_.exchange(offer, std::memory_order_acq_rel);
  if (unseen > kSilence) destroy(reinterpret_cast<Sound*>(unseen));   // never taken by the audio thread
  newest_ = raw;
}

Sound* SoundLine::playing() {
  reclaim();
  return newest_;
}

// --- audio thread ---------------------------------------------------------

// Into an empty hand-back slot; parked here while every slot is full, and
// offered again at the next buffer. A swap needs a play() call, which
// empties the slots first, and hands back at most two sounds (the one cut,
// and the one whose fade ends), so the slots do not fill in practice.
void SoundLine::hand_back(Sound* s) noexcept {
  for (auto& slot : handedBack_) {
    Sound* empty = nullptr;
    if (slot.compare_exchange_strong(empty, s, std::memory_order_acq_rel)) return;
  }
  for (auto& parked : parked_)
    if (!parked) {
      parked = s;
      return;
    }
}

// The next sound plays; the one it replaces fades; one still fading from an
// earlier swap is cut and handed back.
void SoundLine::take(Sound* next) noexcept {
  if (fading_) hand_back(fading_);
  fading_ = current_;
  fadeLeft_ = fading_ ? fadeSamples_ : 0;
  current_ = next;
}

void SoundLine::begin_buffer() noexcept {
  for (auto& parked : parked_) {
    Sound* s = parked;
    parked = nullptr;
    if (s) hand_back(s);
  }
  const std::uintptr_t offer = offered_.exchange(kNothing, std::memory_order_acq_rel);
  if (offer != kNothing) take(offer == kSilence ? nullptr : reinterpret_cast<Sound*>(offer));
}

void SoundLine::fill_chunk(float* stereo, int frames) noexcept {
  if (current_) current_->fill(stereo, frames);
  else std::fill(stereo, stereo + 2 * frames, 0.0f);
  if (!fading_) return;
  fading_->fill(fadingStereo_.data(), frames);
  for (int i = 0; i < frames && fadeLeft_ > 0; ++i, --fadeLeft_) {
    const float gain = float(fadeLeft_) / float(fadeSamples_);
    stereo[2 * i] += fadingStereo_[size_t(2 * i)] * gain;
    stereo[2 * i + 1] += fadingStereo_[size_t(2 * i + 1)] * gain;
  }
  if (fadeLeft_ == 0) {
    hand_back(fading_);
    fading_ = nullptr;
  }
}

}  // namespace mforce::ui
