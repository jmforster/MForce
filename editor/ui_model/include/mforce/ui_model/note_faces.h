#pragma once
#include "mforce/patch/patch.h"

namespace mforce::ui {

// Every PerformNode no face names is given to a face: one node of each
// field per face, in the graph's order; a node goes to the first face whose
// slot for its field is free, and a new face takes the next free label
// (fresh_face_label). Returns how many faces it made. A patch whose
// PerformNodes are all named is left as it is.
//
// A face's node is not on the canvas, so a node given to a face loses its
// entry in layout.positions; a new face takes the position the file gave its
// first node, when it gave one, and otherwise has none until placement gives
// it one. The layout is the editor's own data: this writes layout.faces
// directly, since no edit of the engine names an existing node for a field.
int gather_note_faces(Patch& patch);

}  // namespace mforce::ui
