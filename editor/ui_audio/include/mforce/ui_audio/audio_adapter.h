#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "mforce/render/live_player.h"

namespace mforce::ui {

// The editor's sound output: the device, its callback, and the player that
// sounds through it.
//
// Two threads use it. The control thread (the editor's own) opens the
// device, hands over players and calls their control functions. The audio
// thread (the device's) calls fill(). Every player is owned by the control
// thread from play() until it is destroyed; the audio thread only borrows
// the ones it fills, and hands each back when it is done with it.
class AudioAdapter {
public:
  AudioAdapter();                                  // no device yet: play() and fill() work, nothing is heard
  ~AudioAdapter();                                 // closes the device first, then destroys every player it holds
  AudioAdapter(const AudioAdapter&) = delete;
  AudioAdapter& operator=(const AudioAdapter&) = delete;

  // The default output: stereo, 512 frames, at 48 kHz or the nearest rate
  // the device has. Throws nothing: when it fails, error() says why. Called
  // once, before the first play().
  void open();
  int  sample_rate() const;                        // the device's rate; 48000 before open()

  // --- control thread ---
  // The player that sounds from the next buffer; the one before it fades
  // over 5 ms and is destroyed later on this thread, never on the audio
  // thread. A null player: silence from the next buffer, the one before it
  // fading as for any other. Throws std::invalid_argument, and changes
  // nothing, when the player's rate is not sample_rate().
  void play(std::unique_ptr<LivePlayer> player);
  LivePlayer* player();                            // the current one, for note_on and set_value; null before the first
  float peak() const;                              // the largest sample, in size, of the buffer last filled
  std::string error() const;                       // why there is no sound, in words; empty when there is

  // --- audio thread ---
  // `frames` stereo frames, interleaved: the current player on both
  // channels, and the one before it while it fades. The device's callback
  // calls this and nothing else; a test calls it directly. Never blocks,
  // allocates, frees, prints or throws.
  void fill(float* stereo, int frames) noexcept;

private:
  struct Device;                                   // RtAudio lives behind this, in the .cpp

  static constexpr int kChunk = 512;               // frames filled at a time; a longer buffer is filled in pieces
  static constexpr int kHandBackSlots = 4;
  // What `offered_` holds besides a player's address: nothing offered yet,
  // or silence. A LivePlayer's address is never 0 or 1.
  static constexpr std::uintptr_t kNothing = 0, kSilence = 1;

  // Control thread.
  void reclaim();                                  // destroys the players the audio thread handed back
  void destroy(LivePlayer* p);

  // Audio thread.
  void take(LivePlayer* next) noexcept;
  void hand_back(LivePlayer* p) noexcept;
  void fill_chunk(float* stereo, int frames, float& peak) noexcept;

  std::unique_ptr<Device> device_;
  int rate_{48000};                                // written by open() before the device starts
  int fadeSamples_{240};                           // 5 ms at rate_

  // Both threads. The newest player offered, as its address (or kNothing,
  // kSilence): the control thread exchanges a new one in, and an address it
  // gets back was never taken, so it destroys that player at once; the audio
  // thread exchanges kNothing in at each fill and takes what it finds. The
  // hand-back slots: the audio thread stores a player it is done with in an
  // empty slot, the control thread empties each slot and destroys what it held.
  std::atomic<std::uintptr_t> offered_{kNothing};
  std::array<std::atomic<LivePlayer*>, kHandBackSlots> handedBack_{};
  std::atomic<float> peak_{0.0f};
  std::atomic<int> deviceFault_{0};                // the last fault the device reported, as RtAudio's error type

  // Control thread only.
  std::vector<std::unique_ptr<LivePlayer>> owned_; // every player not yet destroyed
  LivePlayer* newest_{nullptr};
  std::string openError_;

  // Audio thread only.
  LivePlayer* current_{nullptr};
  LivePlayer* fading_{nullptr};
  int fadeLeft_{0};
  std::array<LivePlayer*, kHandBackSlots> parked_{};   // handed back while every slot was full
  std::array<float, kChunk> mono_{};
  std::array<float, kChunk> fadingMono_{};
};

}  // namespace mforce::ui
