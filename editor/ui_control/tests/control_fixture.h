#pragma once
// The instrument the control tests build on, made through the engine's edits
// so it builds and plays: a note face Note1 whose frequency (__perf_frequency)
// feeds a sine osc, the patch's output, with an Envelope env as its amplitude;
// standing by, a second sine lfo and a MultiSource l with a list pin.
#include "mforce/patch/edit.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/session.h"
#include <algorithm>
#include <string>
#include <vector>

namespace control_fixture {

using namespace mforce;
using namespace mforce::ui;

inline Patch control_patch() {
  Patch p;
  (void)set_instrument(p, InstrumentBlock{});
  (void)add_face(p, "Note1", Vec2{-300, 0});
  const std::string pitch = face_field(p, "Note1", "frequency");
  (void)add_node(p, "Envelope", "env", 1, Vec2{-300, 200});
  (void)add_node(p, "SineSource", "osc", 2, Vec2{0, 0});
  (void)add_node(p, "SineSource", "lfo", 3, Vec2{-600, 0});
  (void)add_node(p, "MultiSource", "l", 4, Vec2{-600, 300});
  (void)connect(p, "osc", "frequency", pitch, false);
  (void)connect(p, "osc", "amplitude", "env", false);
  (void)set_output(p, "osc");
  return p;
}

// `s` holding `p` as a patch just opened from no file, the sound following it.
inline void start(Session& s, const Patch& p) {
  s.open = OpenPatch{p, {}, History{}};
  s.status.clear();
  sound_follows(s);
}

inline const Node* node_of(const Session& s, const std::string& id) {
  for (const auto& n : s.open.patch.graph.nodes)
    if (n.id == id) return &n;
  return nullptr;
}

inline size_t steps(const Session& s) { return s.open.history.past.size(); }

// What the mixed output holds over `buffers` buffers: whether any sample is not 0.
inline bool sounds(Session& s, int buffers) {
  std::vector<float> out(512);
  bool any = false;
  for (int k = 0; k < buffers; ++k) {
    s.audio.fill(out.data(), 256);
    any = any || std::any_of(out.begin(), out.end(), [](float x) { return x != 0.0f; });
  }
  return any;
}

}  // namespace control_fixture
