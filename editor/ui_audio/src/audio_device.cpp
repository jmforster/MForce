// The AudioAdapter's device: RtAudio, opened on the default output, its
// callback, and the faults it reports in words. The swap of players and the
// fill are in audio_adapter.cpp.
#include "mforce/ui_audio/audio_adapter.h"
#include <algorithm>
#include <cstdlib>
#include <exception>
#include <string>
#include <RtAudio.h>

namespace mforce::ui {

namespace {

constexpr unsigned int kWantedRate = 48000;
constexpr unsigned int kFrames = 512;

// 48 kHz when the device has it, else the rate it lists nearest to it, else
// the rate it prefers.
unsigned int nearest_rate(const RtAudio::DeviceInfo& info) {
  if (info.sampleRates.empty()) return info.preferredSampleRate ? info.preferredSampleRate : kWantedRate;
  return *std::min_element(info.sampleRates.begin(), info.sampleRates.end(), [](unsigned int a, unsigned int b) {
    return std::abs(int(a) - int(kWantedRate)) < std::abs(int(b) - int(kWantedRate));
  });
}

// The device's thread calls this for each buffer; it fills and nothing else.
int on_buffer(void* out, void* /*in*/, unsigned int frames, double /*time*/, RtAudioStreamStatus /*status*/,
              void* adapter) {
  static_cast<AudioAdapter*>(adapter)->fill(static_cast<float*>(out), int(frames));
  return 0;
}

const char* fault_words(int type) {
  switch (type) {
    case RTAUDIO_NO_DEVICES_FOUND:  return "it finds no device";
    case RTAUDIO_INVALID_DEVICE:    return "the device is not valid";
    case RTAUDIO_DEVICE_DISCONNECT: return "the device was disconnected";
    case RTAUDIO_MEMORY_ERROR:      return "it ran out of memory";
    case RTAUDIO_INVALID_PARAMETER: return "a setting was refused";
    case RTAUDIO_INVALID_USE:       return "it was used wrongly";
    case RTAUDIO_DRIVER_ERROR:      return "the driver failed";
    case RTAUDIO_SYSTEM_ERROR:      return "the system failed";
    case RTAUDIO_THREAD_ERROR:      return "its thread failed";
    default:                        return "an unknown fault";
  }
}

}  // namespace

// RtAudio, with its fault reports kept as one number the control thread
// reads: the device reports from its own thread, so the words are made on
// the reading side and no text is shared.
struct AudioAdapter::Device {
  RtAudio rt;
  unsigned int id{0};                              // the device the stream was opened on
  std::string name;
  explicit Device(std::atomic<int>& fault)
      : rt(RtAudio::UNSPECIFIED, [&fault](RtAudioErrorType type, const std::string&) {
          if (type != RTAUDIO_WARNING) fault.store(int(type), std::memory_order_relaxed);
        }) {
    rt.showWarnings(false);
  }
  // stopStream returns once the device's thread has ended (it waits for the
  // thread), so no callback runs after this; a thread that ended by itself
  // leaves the stream stopped, and closing it then waits for nothing.
  ~Device() {
    if (rt.isStreamRunning()) rt.stopStream();
    if (rt.isStreamOpen()) rt.closeStream();
  }
  Device(const Device&) = delete;
  Device& operator=(const Device&) = delete;
};

AudioAdapter::AudioAdapter() = default;

// The device first, so its thread no longer fills; then the sounds.
AudioAdapter::~AudioAdapter() { device_.reset(); }

void AudioAdapter::open() {
  asked_ = true;
  start_device();
}

// No device is open when this begins: open() runs once, and restart() has
// closed the one before. So the fades are written while no callback runs.
void AudioAdapter::start_device() {
  openError_.clear();
  try {
    device_ = std::make_unique<Device>(deviceFault_);
    RtAudio& rt = device_->rt;
    if (rt.getDeviceCount() == 0) {
      openError_ = "there is no sound output device";
      device_.reset();
      return;
    }
    RtAudio::StreamParameters out;
    out.deviceId = rt.getDefaultOutputDevice();
    out.nChannels = 2;
    unsigned int frames = kFrames;
    const RtAudio::DeviceInfo info = rt.getDeviceInfo(out.deviceId);
    const unsigned int rate = nearest_rate(info);
    device_->id = out.deviceId;
    device_->name = info.name;
    if (rt.openStream(&out, nullptr, RTAUDIO_FLOAT32, rate, &frames, &on_buffer, this) != RTAUDIO_NO_ERROR) {
      openError_ = "the sound device did not open: " + rt.getErrorText();
      device_.reset();
      return;
    }
    rate_ = int(rt.getStreamSampleRate());
    for (SoundLine& l : lines_) l.set_fade(rate_ / 200);
    if (rt.startStream() != RTAUDIO_NO_ERROR) {
      openError_ = "the sound device did not start: " + rt.getErrorText();
      device_.reset();
      return;
    }
    deviceFault_.store(0, std::memory_order_relaxed);
  } catch (const std::exception& e) {
    openError_ = std::string("the sound device did not open: ") + e.what();
    device_.reset();
  }
}

int AudioAdapter::sample_rate() const { return rate_; }

bool AudioAdapter::asked_to_open() const { return asked_; }

std::string AudioAdapter::restart() {
  if (!asked_) return "no sound device was ever opened";
  device_.reset();                                 // the stream closed: no callback runs from here
  start_device();
  return openError_;
}

bool AudioAdapter::running() const { return device_ && device_->rt.isStreamRunning(); }

bool AudioAdapter::default_device_changed() {
  return device_ && device_->rt.getDefaultOutputDevice() != device_->id;
}

std::string AudioAdapter::device_name() const { return device_ ? device_->name : std::string(); }

std::string AudioAdapter::error() const {
  if (!openError_.empty()) return openError_;
  if (!device_) return "no sound device is open";
  const int fault = deviceFault_.load(std::memory_order_relaxed);
  if (fault == 0) return {};
  return std::string("the sound device reported a fault: ") + fault_words(fault);
}

}  // namespace mforce::ui
