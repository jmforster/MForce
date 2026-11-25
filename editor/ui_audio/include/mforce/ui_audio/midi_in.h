#pragma once
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace mforce::ui {

// One MIDI message as it arrived: its status byte and up to two data bytes
// (0 where the message has fewer).
struct MidiMessage { unsigned char status{0}, data1{0}, data2{0}; };

// The computer's MIDI input. RtMidi lives behind this, in the .cpp, as RtAudio
// does behind the AudioAdapter: it is made the first time a port is asked for
// or opened, so one never asked touches nothing. One thread uses it (the
// editor's own); the library queues what arrives between two polls. Nothing
// throws out of it: the library's errors come back as words.
class MidiIn {
public:
  MidiIn();
  ~MidiIn();                                       // closes the port that is open
  MidiIn(const MidiIn&) = delete;
  MidiIn& operator=(const MidiIn&) = delete;

  std::vector<std::string> ports();                // asked afresh each call: a keyboard plugged in later appears
  std::string open(int port);                      // the port before it closed first; empty, or why not
  void close();
  std::string open_port() const;                   // its name; empty: none
  int poll(std::span<MidiMessage> into);           // the messages since the last call, as many as fit; never blocks

private:
  struct Device;                                   // RtMidi lives behind this, in the .cpp
  Device* device();                                // made on the first ask; null when the library would not start
  std::unique_ptr<Device> device_;
  std::string startError_;                         // why the library would not start, the last time it was asked
  std::string portName_;                           // the port open; empty for none
};

}  // namespace mforce::ui
