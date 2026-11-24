#include "mforce/ui_control/edits.h"
#include <optional>
#include <string>
#include <utility>
#include "mforce/patch/edit.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_model/placement.h"
#include "stepping.h"

namespace mforce::ui {

using namespace stepping;   // the shared helpers by their short names (C#: a `using` directive)

namespace {

// Every position edit is one gesture, so one drag of several things is one step.
const std::string kMove = "move";

}  // namespace

void edit_value(Session& s, std::string_view node, std::string_view pin, float v) {
  const std::string gesture = "value:" + std::string(node) + "." + std::string(pin);
  if (!step(s, gesture, [&](Patch& p) { return set_value(p, node, pin, v); })) return;
  if (!s.open.patch.instrument) return;          // a plain sound running hears it at end_gesture
  LivePlayer* player = s.live.player();
  if (!player || !player->set_value(node, pin, v)) sound_follows(s);
}

void edit_setting(Session& s, std::string_view node, std::string_view name, float v) {
  if (step(s, "", [&](Patch& p) { return set_setting(p, node, name, v); })) sound_follows(s);
}

void edit_string(Session& s, std::string_view node, std::string_view name, std::string v) {
  if (step(s, "", [&](Patch& p) { return set_string(p, node, name, v); })) sound_follows(s);
}

void edit_position(Session& s, std::string_view nodeOrFace, Vec2 pos) {
  (void)step(s, kMove, [&](Patch& p) {
    return has_face(p, nodeOrFace) ? move_face(p, nodeOrFace, pos) : move_node(p, nodeOrFace, pos);
  });
}

void edit_output_position(Session& s, Vec2 pos) {
  (void)step(s, kMove, [&](Patch& p) { return move_output(p, pos); });
}

void pan_canvas(Session& s, Vec2 pos) {
  try {
    (void)set_panning(s.open.patch, pos);
  } catch (const std::exception& e) {
    s.status = e.what();
  }
}

void select_node(Session& s, std::string_view nodeOrFace) { s.selected = std::string(nodeOrFace); }

void arrange(Session& s) {
  (void)step(s, "", [](Patch& p) {
    (void)arrange_nodes(p);
    return Change::Layout;
  });
}

}  // namespace mforce::ui
