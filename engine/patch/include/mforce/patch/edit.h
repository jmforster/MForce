#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include "mforce/patch/instrument_block.h"
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

// The structural edits below all work the same way. Each first finds what
// it names (a node, a pin, a face, a feed's number) and refuses, in its own
// words, a name that is not there. It then makes the change on a copy of the
// patch and asks check() of the copy: the edit refuses exactly what check()
// refuses, with check()'s sentence, and otherwise the copy becomes the patch.
// Each returns Change::Structure unless it says otherwise.
//
// A node a note face names for a field, that an edit leaves with nothing
// reading it (no feed, no dynamic pin, not the output), goes with its field;
// only the nodes the edit itself stopped reading are looked at.

// A node of `type` under `id` at `pos`, holding what a file's node with only
// an id, a type and a seed is read as: every Param and setting at its type's
// declared default. An Envelope starts with the four stages of an ADSR (0.05,
// 0.1, 0.7, 0.2) and a CurveNode with the two knots (0,0) and (1,1). Refused
// on a position that is not finite, and as check() refuses an unregistered
// type and an id that is empty or taken.
Change add_node(Patch&, std::string_view type, std::string_view id, std::uint32_t seed, Vec2 pos);
// An id nothing in the patch has, for a new node of `type`: the type's name
// without a trailing "Source" ("Sine"; "Curve" for a CurveNode, "Note" for a
// PerformNode, "Channel" for a SoundChannel, "Mixer" for a StereoMixer), then
// with 2, 3... Ids, group names and face labels share one namespace.
std::string fresh_id(const Patch&, std::string_view type);
// A label nothing in the patch has, for a new note face: "Note1", "Note2"...
std::string fresh_face_label(const Patch&);
// The node goes, and everything that named it: each feed that read it (a
// Param left with no feed takes its declared default), each dynamic pin it
// fed, its position, its place in groups, its field of a note face. A patch
// whose output it was has none.
Change remove_node(Patch&, std::string_view node);

// `source` feeds `pin` of `node`, through a tap when `tap`. A pin that takes
// one feed loses the one it had; a list takes one more. The pin's number goes.
Change connect(Patch&, std::string_view node, std::string_view pin, std::string_view source, bool tap);
// Feed number `feed` (from 0) of the pin goes. A Param left with none takes
// its declared default.
Change disconnect(Patch&, std::string_view node, std::string_view pin, std::size_t feed);
// A setting driven once per note by `source`, and the drive removed.
Change feed_setting  (Patch&, std::string_view node, std::string_view setting, std::string_view source);
Change unfeed_setting(Patch&, std::string_view node, std::string_view setting);
// The node whose signal is the patch's output; empty for none.
Change set_output(Patch&, std::string_view node);

// A new id for a node, in every place that named it: feeds, dynamic pins,
// the output, its position, group members, face fields. Its own id again
// changes nothing.
Change rename_node(Patch&, std::string_view node, std::string_view to);
// Columns of a node's arrays, all at once (a table's columns keep one
// length). Each row a finite number.
Change set_arrays(Patch&, std::string_view node, const std::map<std::string, std::vector<float>>& columns);
// The instrument's own settings, the volume and the glide finite. A plain
// sound given a block becomes an instrument.
Change set_instrument(Patch&, const InstrumentBlock& block);

// Note faces. A face is the one node the canvas shows for the PerformNodes it
// names, one per field.
Change add_face   (Patch&, std::string_view label, Vec2 pos);              // Layout; a face naming no node yet
Change move_face  (Patch&, std::string_view label, Vec2 pos);              // Layout
Change rename_face(Patch&, std::string_view label, std::string_view to);   // Layout; group members follow
Change remove_face(Patch&, std::string_view label);                        // its PerformNodes go with it
// The id of the node that reads `field` for this face, made now when the
// face names none: a PerformNode of that field under "__perf_<field>", then
// with 2, 3... connect() takes the id as a source. `field` is one of the
// note's fields (frequency, velocity, wheel, pressure, duration, onset).
std::string face_field(Patch&, std::string_view label, std::string_view field);

} // namespace mforce
