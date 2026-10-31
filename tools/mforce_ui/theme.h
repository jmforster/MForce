#pragma once
#include "imgui.h"
#include "mforce/core/source_registry.h"

// The editor's colours, keyed by family and by the editor's own nodes, with
// built-in defaults. Node colours follow the family the registry declares; the
// editor asks nothing else about a type to colour it. Loading a theme from
// JSON and the rest of the editor's colours are Part 7's; this is the object
// they land in.
struct Theme {
    ImU32 family[mforce::kFamilyCount];   // indexed by int(mforce::Family)
    ImU32 output;          // the output, channel and mixer nodes
    ImU32 parameter;       // the parameter node
    ImU32 note;            // the note (performance) node
    ImU32 curve;           // the curve node: a transfer, drawn in its own dark gold
    ImU32 unknown;         // a type the registry does not know, and the Utility family
    ImU32 dynPinGold;      // "set once at note-on, frozen for the note": the dynamic pins, their links, the note node
    ImU32 envelopeStrip;   // the strip views' envelope trace
};

inline constexpr Theme kDefaultTheme = {
    {
        IM_COL32(95, 165, 150, 255),    // Oscillator  — greenish teal
        IM_COL32(185, 95, 85, 255),     // Noise       — reddish orange
        IM_COL32(155, 150, 110, 255),   // Wavetable   — olive tan
        IM_COL32(195, 145, 90, 255),    // Additive    — orange
        IM_COL32(95, 165, 150, 255),    // Physical    — greenish teal
        IM_COL32(145, 130, 175, 255),   // Filter      — dark lavender
        IM_COL32(200, 185, 110, 255),   // Envelope    — pale gold
        IM_COL32(95, 165, 150, 255),    // Modulator   — greenish teal
        IM_COL32(95, 165, 150, 255),    // Combiner    — greenish teal
        IM_COL32(128, 128, 128, 255),   // Utility     — grey
    },
    IM_COL32(120, 130, 145, 255),       // output     — blue grey
    IM_COL32(190, 140, 170, 255),       // parameter  — pink
    IM_COL32(205, 170, 60, 255),        // note       — gold
    IM_COL32(170, 145, 75, 255),        // curve      — dark gold
    IM_COL32(128, 128, 128, 255),       // unknown    — grey
    IM_COL32(205, 170, 60, 255),        // dynPinGold — the performance cadence
    IM_COL32(220, 220, 60, 255),        // envelopeStrip — yellow
};

// The theme in force: the defaults until Part 7 loads one.
inline const Theme& theme() { return kDefaultTheme; }
