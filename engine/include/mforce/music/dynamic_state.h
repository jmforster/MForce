#pragma once
#include "mforce/music/structure.h"

namespace mforce {

// ---------------------------------------------------------------------------
// DynamicState — tracks current velocity from dynamic markings.
// Shared between Conductor (perform tree-walk, retiring) and Composer
// (compose-time realize step).
// ---------------------------------------------------------------------------
struct DynamicState {
  float currentVelocity;
  float targetVelocity;
  float rampStartBeat;
  float rampEndBeat;

  explicit DynamicState(Dynamic d = Dynamic::mf)
    : currentVelocity(dynamic_to_velocity(d))
    , targetVelocity(currentVelocity)
    , rampStartBeat(0), rampEndBeat(0) {}

  void set_marking(const DynamicMarking& m, float passageBeatOffset) {
    float markBeat = passageBeatOffset + m.beat;
    if (m.rampBeats <= 0) {
      currentVelocity = dynamic_to_velocity(m.level);
      targetVelocity = currentVelocity;
    } else {
      rampStartBeat = markBeat;
      rampEndBeat = markBeat + m.rampBeats;
      targetVelocity = dynamic_to_velocity(m.level);
    }
  }

  float velocity_at(float beat) {
    if (beat >= rampEndBeat || rampEndBeat <= rampStartBeat) {
      currentVelocity = targetVelocity;
      return currentVelocity;
    }
    if (beat <= rampStartBeat) return currentVelocity;
    float t = (beat - rampStartBeat) / (rampEndBeat - rampStartBeat);
    return currentVelocity + t * (targetVelocity - currentVelocity);
  }
};

} // namespace mforce
