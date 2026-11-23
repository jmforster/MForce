#pragma once
#include "mforce/patch/patch.h"

namespace mforce::ui {

// The panning the canvas shows the patch at: the saved one; with none saved,
// one that puts the top-left corner of the nodes and the Output node a small
// margin from the canvas's top-left corner, so a patch opens in view. (The
// canvas draws a point p of the record at p plus the panning.)
Vec2 canvas_panning(const Patch& patch);

}  // namespace mforce::ui
