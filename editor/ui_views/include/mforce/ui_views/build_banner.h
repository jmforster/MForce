#pragma once
#include "mforce/ui_control/session.h"
#include "mforce/ui_views/places.h"

namespace mforce::ui {

// The stale banner: while the program is older than one of its sources and
// it was not dismissed (banner_shown), a dark red window centred at the top
// of the screen with the stamp's two lines and "Dismiss" (dismiss_banner).
class BuildBanner {
public:
  void draw(Session& s);
  // Where "banner:Dismiss" was drawn last frame.
  const Places& places() const { return places_; }

private:
  Places places_;
};

}  // namespace mforce::ui
