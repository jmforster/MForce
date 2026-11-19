#pragma once
#include <string>
#include <string_view>

namespace mforce {

// The sentence a file that carries instrument.paramMap is refused with, by the
// loader, by the codec and by the editor, which refuses before it touches its
// open graph.
inline constexpr char kParamMapRefusal[] = "instrument.paramMap is no longer read: python tools/convert_patch.py <patch> rewrites it as wiring";

// Where a retired spelling sat: a key of a node's params, a key on one of a
// pin's feed objects, an object given where a slot takes a feed, a key of the
// node object beside params, or a key at the top of the document.
enum class RetiredAt { Key, FeedKey, InlineSlot, NodeKey, TopKey };

// The sentence a spelling the engine does not read is refused with, by node
// type, key and where the key sat, naming the converter that rewrites it and
// what it becomes; empty for any other triple. A row with an empty type
// answers for every type. One table holds every row: a table's row form (an
// Envelope's or a PhasedValueSource's stages, a CurveNode's knots and
// exprKnots), a key under another name (cutoff, gap), a MultiSource feed's
// weight and delay, an inline expandRule on a Partials host, the `inputs`
// block beside a SoundChannel's or a StereoMixer's params, and the document's
// ui, groups, _comment and _provenance. The loader and the codec throw a
// node's row after the node's id and type (and, for a feed key, the pin), and
// so does the editor, before it touches its open graph. The loader asks every
// place but TopKey; the codec and the editor ask every place.
std::string retired_key_refusal(std::string_view type, std::string_view key,
                                RetiredAt at = RetiredAt::Key);

} // namespace mforce
