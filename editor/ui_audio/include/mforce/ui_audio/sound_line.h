#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>
#include "mforce/ui_audio/sound.h"

namespace mforce::ui {

// One line of the output: the sound that plays on it, and the hand-over of
// a new one from the control thread to the audio thread.
//
// Every sound is owned by the control thread from play() until it is
// destroyed there; the audio thread only borrows the ones it fills, and
// hands each back when it is done with it. One atomic word carries the
// newest sound offered; the one it replaces fades over the fade's length
// and goes back through four atomic hand-back slots.
class SoundLine {
public:
  // The most frames fill_chunk is given at once.
  static constexpr int kChunk = 512;

  SoundLine() = default;
  SoundLine(const SoundLine&) = delete;
  SoundLine& operator=(const SoundLine&) = delete;

  // While no callback runs: the fade of the sound replaced; a fade under way
  // is cut to the new length.
  void set_fade(int samples);

  // --- control thread ---
  // The sound that plays from the next buffer; null: silence. The one before
  // it fades and is destroyed later on this thread, never on the audio
  // thread.
  void play(std::unique_ptr<Sound> sound);
  // The newest sound handed over; null for none. Destroys what the audio
  // thread handed back.
  Sound* playing();

  // --- audio thread ---
  // `frames` (at most kChunk) stereo frames WRITTEN to `stereo`: the current
  // sound, and the one before it while it fades. Never blocks, allocates,
  // frees or throws.
  void fill_chunk(float* stereo, int frames) noexcept;
  // Once at the top of each buffer: a new sound offered is taken, and a
  // sound parked for want of a slot is offered back again.
  void begin_buffer() noexcept;

private:
  // What `offered_` holds besides a sound's address: nothing offered yet, or
  // silence. A Sound's address is never 0 or 1.
  static constexpr std::uintptr_t kNothing = 0, kSilence = 1;
  static constexpr int kHandBackSlots = 4;

  // Control thread.
  void reclaim();
  void destroy(Sound* s);

  // Audio thread.
  void take(Sound* next) noexcept;
  void hand_back(Sound* s) noexcept;

  int fadeSamples_{240};

  // Both threads: the newest sound offered, as its address (or kNothing,
  // kSilence); the sounds handed back.
  std::atomic<std::uintptr_t> offered_{kNothing};
  std::array<std::atomic<Sound*>, kHandBackSlots> handedBack_{};

  // Control thread only.
  std::vector<std::unique_ptr<Sound>> owned_;
  Sound* newest_{nullptr};

  // Audio thread only.
  Sound* current_{nullptr};
  Sound* fading_{nullptr};
  int fadeLeft_{0};
  std::array<Sound*, kHandBackSlots> parked_{};
  std::array<float, 2 * kChunk> fadingStereo_{};
};

}  // namespace mforce::ui
