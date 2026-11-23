#pragma once
#include "mforce/render/voice.h"
#include <utility>
#include <vector>

namespace mforce {

// An instrument's voices and the scheduled rotation: each fresh note takes
// the next voice in turn. Overlap on the scheduled path is fixed by the
// score's timing, so a blind rotation suffices.
class VoicePool {
public:
  void add(Voice voice) { voices_.push_back(std::move(voice)); }
  int size() const { return int(voices_.size()); }
  bool empty() const { return voices_.empty(); }
  Voice& voice(int index) { return voices_[size_t(index)]; }
  const Voice& voice(int index) const { return voices_[size_t(index)]; }

  // The voice the next fresh note takes; the rotation moves on.
  int next_scheduled() {
    const int v = int(next_ % size());
    ++next_;
    return v;
  }

  auto begin() { return voices_.begin(); }
  auto end() { return voices_.end(); }
  auto begin() const { return voices_.begin(); }
  auto end() const { return voices_.end(); }

private:
  std::vector<Voice> voices_;
  int next_{0};
};

} // namespace mforce
