#pragma once
#include "mforce/ui_control/session.h"
#include "mforce/ui_views/places.h"

namespace mforce::ui {

// The bar above the canvas while the patch has a group or the canvas shows
// one: the file's stem ("Patch" for a patch never saved), then " > " and a
// button for each group gone into; a button goes up to its level (go_up).
// At its right, inside a group that has an output, "Listen:" with "Patch"
// (stop_listening) and "Group" (listen_at_group), the one in force green.
class Breadcrumb {
public:
  static bool shown(const Session& s);
  void draw(Session& s);
  // Where each button was drawn last frame: "crumb:0" (the patch),
  // "crumb:1"..., "listen:Patch", "listen:Group".
  const Places& places() const { return places_; }

private:
  void listen_switch(Session& s);
  Places places_;
};

}  // namespace mforce::ui
