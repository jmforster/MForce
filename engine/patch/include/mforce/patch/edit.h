#pragma once
#include <string>
#include <string_view>
#include "mforce/patch/patch.h"

namespace mforce {

// What a host must do about an edit it has applied.
enum class Change { Value,       // a pin's constant: the sound can follow without a rebuild
                    Structure,   // anything the build reads: build again
                    Layout };    // the editor's own data: the sound is untouched

// Each edit applies whole, or throws std::runtime_error with one sentence
// naming the node, the name and the reason, and leaves the patch as it was.
// A patch that passed check() before an accepted edit passes it after. A
// node id names the first node that has it, as it does for check().

// A pin's constant. Refused on a pin a wire feeds and on a slot; a list
// pin with no feed takes a number as any signal pin does.
Change set_value  (Patch&, std::string_view node, std::string_view pin, float v);
// A scalar setting (a float, an int, a bool as 0 or 1, an enum as its index).
// Refused outside the declared range or labels.
Change set_setting(Patch&, std::string_view node, std::string_view name, float v);
// A string setting.
Change set_string (Patch&, std::string_view node, std::string_view name, std::string v);
// Where a node sits on the canvas.
Change move_node  (Patch&, std::string_view node, Vec2 pos);
// Where the canvas's Output node sits, and the canvas's panning.
Change move_output(Patch&, Vec2 pos);
Change set_panning(Patch&, Vec2 pos);

} // namespace mforce
