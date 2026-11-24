#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include "mforce/ui_audio/sound.h"
#include "mforce/ui_audio/sound_line.h"

namespace mforce::ui {

// The editor's sound output: the device, its callback, and the two lines of
// sound that play through it, summed.
//
// Two threads use it. The control thread (the editor's own) opens the
// device and hands sounds over; the audio thread (the device's) calls
// fill(). Each line hands its sounds over as SoundLine says.
class AudioAdapter {
public:
  // What the keys play, and what Play plays: two at once, summed.
  enum class Line { Keys, Playback };

  AudioAdapter();                                  // no device yet: play() and fill() work, nothing is heard
  ~AudioAdapter();                                 // closes the device first, then destroys every sound it holds
  AudioAdapter(const AudioAdapter&) = delete;
  AudioAdapter& operator=(const AudioAdapter&) = delete;

  // The default output: stereo, 512 frames, at 48 kHz or the nearest rate
  // the device has. Throws nothing: when it fails, error() says why. Called
  // once, before the first play().
  void open();
  int  sample_rate() const;                        // the device's rate; 48000 before open()
  bool asked_to_open() const;                      // open() has been called, whether or not it worked

  // --- the device's health, control thread ---
  // Closes the stream, then opens the default output as open() does: the
  // error of an open that failed before is forgotten, the lines' fades are
  // set from the new rate before the new stream starts. Empty, or why not
  // (error() says it too). The rate may be another: the caller rebuilds
  // what it plays. On an adapter never asked to open it does nothing and
  // says so. Closing waits until the device's thread has ended.
  std::string restart();
  std::uint64_t heartbeat() const;                 // how many buffers fill() has made
  bool running() const;                            // a stream is open and running
  bool default_device_changed();                   // the system's default output is not the device opened (asks the system)
  std::string device_name() const;                 // the device opened; empty for none

  // --- control thread ---
  // The sound that plays on `line` from the next buffer; null: silence. The
  // one before it fades over 5 ms and is destroyed later on this thread.
  void   play(Line line, std::unique_ptr<Sound> sound);
  Sound* playing(Line line);                       // the current one; null when none
  float  peak() const;                             // the largest sample, in size, of the last buffer, after the clip
  float  peak_before_clip() const;                 // and before it
  std::string error() const;                       // why there is no sound, in words; empty when there is

  // --- audio thread ---
  // `frames` stereo frames, interleaved: each line's sound, the two summed,
  // the sum soft-clipped once. The device's callback calls this and nothing
  // else; a test calls it directly. Never blocks, allocates, frees, prints
  // or throws.
  void fill(float* stereo, int frames) noexcept;

private:
  struct Device;                                   // RtAudio lives behind this, in the .cpp
  void start_device();                             // open()'s work, and restart()'s

  SoundLine& line(Line l) { return lines_[l == Line::Keys ? 0 : 1]; }

  std::unique_ptr<Device> device_;
  int rate_{48000};                                // written by open() before the device starts
  std::array<SoundLine, 2> lines_;
  std::atomic<float> peak_{0.0f};
  std::atomic<float> peakBeforeClip_{0.0f};
  std::atomic<int> deviceFault_{0};                // the last fault the device reported, as RtAudio's error type
  std::atomic<std::uint64_t> beats_{0};            // written by fill (audio thread), read by heartbeat()
  std::string openError_;                          // control thread only
  bool asked_{false};                              // control thread only

  // Audio thread only.
  std::array<float, 2 * SoundLine::kChunk> keys_{};
  std::array<float, 2 * SoundLine::kChunk> playback_{};
};

}  // namespace mforce::ui
