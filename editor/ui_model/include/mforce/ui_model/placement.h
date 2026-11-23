#pragma once
#include "mforce/patch/patch.h"

namespace mforce::ui {

// A position for every node that has none, and for the canvas's Output node
// when it has none. Returns how many it placed. A node that has a position
// keeps it. The same patch always gets the same positions.
//
// Columns by distance from the output: the node graph.output names is column
// 0, a node whose nearest reader is in column k is in column k + 1, further
// left; a node nothing reads and that reaches the output through nothing is
// in a last column. Wires, taps and dynamic pins are all reads. In a column,
// nodes go top to bottom in the graph's order, each in a slot as tall as its
// declared pins need. The Output node sits one column right of column 0.
// When some nodes have positions, the unplaced ones are laid out as one
// block below the placed nodes.
int place_unplaced_nodes(Patch& patch);

}  // namespace mforce::ui
