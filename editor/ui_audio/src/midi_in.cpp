// MidiIn: RtMidi's input, made on the first ask, its errors kept as words.
#include "mforce/ui_audio/midi_in.h"
#include <exception>
#include <string>
#include <vector>
#include <RtMidi.h>

namespace mforce::ui {

// RtMidi's input and the words of the last error it reported. With an error
// callback set, RtMidi calls it in place of printing and throwing; it calls
// it only from inside the calls this file makes, on this thread.
struct MidiIn::Device {
  RtMidiIn in;
  std::string error;                               // the last error since it was cleared; empty for none
  std::vector<unsigned char> bytes;                // one message, reused by poll
  Device() {
    in.setErrorCallback(&Device::on_error, this);
  }
  Device(const Device&) = delete;
  Device& operator=(const Device&) = delete;
  // A plain function, as RtMidi takes: `self` is the Device it was given.
  static void on_error(RtMidiError::Type type, const std::string& text, void* self) {
    if (type != RtMidiError::WARNING && type != RtMidiError::DEBUG_WARNING) static_cast<Device*>(self)->error = text;
  }
};

MidiIn::MidiIn() = default;

// Defined here, where Device is complete, so unique_ptr can destroy it; the
// library closes an open port as it is destroyed.
MidiIn::~MidiIn() = default;

MidiIn::Device* MidiIn::device() {
  if (device_) return device_.get();
  try {
    device_ = std::make_unique<Device>();
    startError_.clear();
  } catch (const std::exception& e) {
    startError_ = std::string("MIDI did not start: ") + e.what();
  }
  return device_.get();
}

std::vector<std::string> MidiIn::ports() {
  std::vector<std::string> names;
  Device* d = device();
  if (!d) return names;
  try {
    const unsigned int count = d->in.getPortCount();
    for (unsigned int p = 0; p < count; ++p) names.push_back(d->in.getPortName(p));
  } catch (const std::exception&) {
    // A port that went away while being listed: the list as far as it got.
  }
  return names;
}

std::string MidiIn::open(int port) {
  close();
  Device* d = device();
  if (!d) return startError_;
  if (port < 0) return "there is no MIDI port " + std::to_string(port);
  try {
    d->error.clear();
    const std::string name = d->in.getPortName(unsigned(port));
    d->in.openPort(unsigned(port));
    if (!d->error.empty()) return d->error;
    if (!d->in.isPortOpen()) return "the MIDI port " + std::to_string(port) + " did not open";
    portName_ = name;
    return {};
  } catch (const std::exception& e) {
    return e.what();
  }
}

void MidiIn::close() {
  portName_.clear();
  if (!device_) return;
  try {
    if (device_->in.isPortOpen()) device_->in.closePort();
  } catch (const std::exception&) {
    // Closed as far as the library could: nothing more is read from it.
  }
}

std::string MidiIn::open_port() const { return portName_; }

int MidiIn::poll(std::span<MidiMessage> into) {
  if (!device_ || portName_.empty()) return 0;
  int n = 0;
  try {
    while (n < int(into.size())) {
      device_->in.getMessage(&device_->bytes);     // the oldest waiting, or nothing: it does not wait
      const std::vector<unsigned char>& b = device_->bytes;
      if (b.empty()) break;
      MidiMessage& m = into[std::size_t(n++)];
      m = MidiMessage{b[0], b.size() > 1 ? b[1] : (unsigned char)0, b.size() > 2 ? b[2] : (unsigned char)0};
    }
  } catch (const std::exception&) {
    // What was read before it is kept.
  }
  return n;
}

}  // namespace mforce::ui
