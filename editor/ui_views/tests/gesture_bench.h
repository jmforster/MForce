#pragma once
// The patch and the bench the arrange gestures run on, under the headless
// editor, and what must still hold after every gesture.
#include "mforce/testing/check.h"
#include "mforce/patch/edit.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/selecting.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_model/groups.h"
#include "mforce/ui_views/canvas_table.h"
#include "mforce/ui_views/headless.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace gesture_bench {

using namespace mforce;
using namespace mforce::ui;
using Names = std::vector<std::string>;

// A note's frequency into osc, env its amplitude, osc the output; lfo
// standing by. Each placed apart enough that a point on one is on no other.
inline Patch gesture_patch() {
  Patch p;
  (void)set_instrument(p, InstrumentBlock{});
  (void)add_face(p, "Note1", Vec2{40, 40});
  (void)add_node(p, "SineSource", "osc", 2, Vec2{300, 40});
  (void)add_node(p, "Envelope", "env", 3, Vec2{600, 40});
  (void)add_node(p, "SineSource", "lfo", 4, Vec2{300, 230});
  (void)connect(p, "osc", "frequency", face_field(p, "Note1", "frequency"), false);
  (void)connect(p, "osc", "amplitude", "env", false);
  (void)set_output(p, "osc");
  p.layout.outputPos = Vec2{860, 40};
  return p;
}

// The session on a patch, drawn until it is settled.
struct Bench {
  Session s;
  HeadlessEditor editor;
  explicit Bench(Patch p = gesture_patch()) : editor(start(s, std::move(p))) { editor.frames(3); }
  static Session& start(Session& s, Patch p) {
    s.open = OpenPatch{std::move(p), {}, History{}};
    sound_follows(s);
    return s;
  }
  // A point on a drawn thing's title bar ("node:<name>", "group:<name>"); a
  // Wormhole has none, so its middle.
  Vec2 grip(const std::string& place) const {
    const auto a = editor.area(place);
    if (!a) return Vec2{-1, -1};
    if (place.rfind("node:Wormhole", 0) == 0) return a->centre();
    return Vec2{a->lo.x + 20.0f, a->lo.y + 8.0f};
  }
  // A point on empty canvas, clear of the mini-map.
  Vec2 empty() const {
    const Vec2 o = editor.canvas_origin(), size = editor.canvas_size();
    return Vec2{o.x + size.x * 0.62f, o.y + size.y - 15.0f};
  }
  bool drawn(const std::string& place) const { return editor.area(place).has_value(); }
};

// What must hold after any gesture: each selected name and each drawn thing
// names something there is, and the path names only groups, each in the one
// before.
inline bool holds(const Session& s) {
  const Patch& p = s.open.patch;
  const auto exists = [&](const std::string& n) {
    for (const auto& node : p.graph.nodes)
      if (node.id == n) return true;
    for (const auto& f : p.layout.faces)
      if (f.label == n) return true;
    for (const auto& g : p.layout.groups)
      if (g.name == n) return true;
    return false;
  };
  bool ok = valid_depth(p, s.groupPath) == s.groupPath.size();
  for (const auto& n : s.selections) ok = ok && exists(n);
  CanvasNames names;
  for (const auto& n : canvas_table(p, s.groupPath, s.offered, names).nodes)
    ok = ok && (n.kind == CanvasNode::Kind::Output || exists(n.name));
  if (!ok) std::printf("  after the gesture the session names something that is not there\n");
  return ok;
}

inline const Group* group_of(const Session& s, const std::string& name) {
  for (const auto& g : s.open.patch.layout.groups)
    if (g.name == name) return &g;
  return nullptr;
}

inline const Node* node_of(const Session& s, const std::string& id) {
  for (const auto& n : s.open.patch.graph.nodes)
    if (n.id == id) return &n;
  return nullptr;
}

}  // namespace gesture_bench
