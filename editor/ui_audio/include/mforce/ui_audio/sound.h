#pragma once

namespace mforce::ui {

// Something the output plays, on the audio thread, at the output's rate
// (that is its maker's business: the adapter checks no rate). A class whose
// functions are all pure virtual is C#'s interface; a sound is one class
// that implements it.
class Sound {
public:
  virtual ~Sound() = default;
  // The next `frames` stereo frames, interleaved, WRITTEN to `stereo`. Never
  // blocks, allocates, frees or throws.
  virtual void fill(float* stereo, int frames) noexcept = 0;
  // Nothing more to play. Read by the control thread while the audio thread
  // fills, so an implementation backs it with an atomic.
  virtual bool over() const noexcept { return false; }
};

}  // namespace mforce::ui
