#pragma once
#include "mforce/patch/patch.h"

namespace mforce::ui {

// A position for every thing the canvas draws that has none: each node no
// note face names, each face, and the canvas's Output node. Returns how many
// it placed. A thing that has a position keeps it; a node a face names is
// not on the canvas and is given none. A position that is not finite, or has
// x or y of 1e7 or more, is no position: it is dropped and the thing placed
// afresh (a file may hold one, and a canvas handed one does not draw). The
// same patch always gets the same positions.
//
// Columns by distance from the output: the node graph.output names is column
// 0, a node whose nearest reader is in column k is in column k + 1, further
// left; a node nothing reads and that reaches the output through nothing is
// in a last column. Wires, taps and dynamic pins are all reads. A face is in
// the nearest column of its nodes. In a column, things go top to bottom in
// the graph's order (a face at its first node's place), each in a slot as
// tall as its rows need: a face has a row for each of the note's six fields.
// The Output node sits one column right of column 0. When some things have
// positions, the unplaced ones are laid out as one block below them.
int place_unplaced_nodes(Patch& patch);

// A fresh position for every node, every face and the Output node, by the
// same rule as place_unplaced_nodes, whatever positions they had; the saved
// panning is dropped, so the canvas looks at the new layout. The groups, and
// the faces' labels and fields, are left as they are. Returns how many
// things it placed, less the Output node.
int arrange_nodes(Patch& patch);

// The size of the slot placement gives a node, a face and the Output node:
// a column's width less a gap, and the height its rows need.
Vec2 node_slot(const Node& n);
Vec2 face_slot();
Vec2 output_slot();

}  // namespace mforce::ui
