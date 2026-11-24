#pragma once
#include "mforce/patch/patch.h"

namespace mforce::ui {

// The panning that puts the top-left corner of what the canvas draws (the
// nodes no face names, the faces and the Output node) a small margin from
// the canvas's top-left corner, whatever panning is saved. (The canvas draws
// a point p of the record at p plus the panning.)
Vec2 fit_panning(const Patch& patch);

// The panning the canvas shows the patch at: the saved one; with none saved,
// fit_panning, so a patch opens in view.
Vec2 canvas_panning(const Patch& patch);

// What the status line counts: the nodes the canvas draws (the graph's nodes
// no face names, and the faces) and its wires (each feed, each dynamic pin,
// and the output when the patch has one).
struct CanvasCounts {
  int nodes{0};
  int wires{0};
};
CanvasCounts canvas_counts(const Patch& patch);

}  // namespace mforce::ui
