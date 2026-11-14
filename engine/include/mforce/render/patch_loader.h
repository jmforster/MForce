#pragma once
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include "mforce/render/mixer.h"
#include "mforce/render/instrument.h"

namespace mforce {

// The sentence a file that carries instrument.paramMap is refused with, by the
// loader and by the editor, which refuses before it touches its open graph.
inline constexpr char kParamMapRefusal[] = "instrument.paramMap is no longer read: python tools/convert_patch.py <patch> rewrites it as wiring";

// The sentence a node's params key is refused with when it is a table's row
// form the engine no longer reads (an Envelope's stages, a CurveNode's knots
// and exprKnots), naming the converter that rewrites it as columns; empty for
// any other type and key. The loader throws it after the node's id and type,
// and so does the editor, before it touches its open graph.
std::string retired_key_refusal(std::string_view type, std::string_view key);

// The table rule, the one both hosts ask: for each array group of `node`,
// its columns are all absent or all present at one length. `length` gives a
// column's length, or -1 when the file gives no array under that name.
// Returns the sentence's tail for the first group that breaks the rule,
// "table '<group>' takes its columns at one length, all present or all
// absent (got ...)", else empty. The loader and the editor throw it after
// the node's id and type.
std::string ragged_table_refusal(const Component& node,
                                 const std::function<int(const char* column)>& length);

struct BuiltSound {
  int sampleRate{48000};
  int frames{48000*5};
  std::unique_ptr<Mixer> mixer;
};

BuiltSound load_patch_file(const std::string& path);

// Load a PitchedInstrument from a patch JSON (no score, no mixer wiring).
// Returns the instrument + sampleRate for external use (e.g. Conductor).
struct InstrumentPatch {
  std::unique_ptr<PitchedInstrument> instrument;
  int sampleRate{48000};
};

// minPolyphony: floor on the voice-pool size (0 = respect the patch's own
// "polyphony" value). Live-keyboard callers pass a floor so patches authored
// with small pools (default 4) still give an 88-key board enough voices.
InstrumentPatch load_instrument_patch(const std::string& path,
                                      int minPolyphony = 0);

// Same as load_instrument_patch, from already-serialized JSON text —
// no file. The UI's Generate path serializes its live editor graph and
// loads it directly (render-capture unification spec 2026-09-13).
InstrumentPatch load_instrument_patch_json(const std::string& jsonText,
                                           int minPolyphony = 0);

} // namespace mforce
