// canvas_panning: the saved panning, or one that brings the nodes' top-left
// corner to the canvas's.
#include "mforce/testing/check.h"
#include "mforce/ui_model/canvas_view.h"

using namespace mforce;
using namespace mforce::ui;

void run_canvas_view_tests() {
  Patch p;
  CHECK(canvas_panning(p) == (Vec2{40, 40}));             // nothing to show: the margin
  p.layout.positions = {{"a", Vec2{-560, 0}}, {"b", Vec2{-280, 120}}};
  p.layout.outputPos = Vec2{280, -30};
  CHECK(canvas_panning(p) == (Vec2{600, 70}));            // the least x and y, a margin in
  p.layout.panning = Vec2{5, 6};
  CHECK(canvas_panning(p) == (Vec2{5, 6}));               // a saved panning wins
}
