#pragma once
// The canvas's colours and the conversions between the record's points and
// ImGui's, shared by the canvas's files. Included only by .cpp files of
// ui_views, which may use ImGui.
#include <algorithm>
#include <string>
#include <imgui.h>
#include "mforce/core/source_registry.h"
#include "mforce/patch/layout.h"

namespace mforce::ui::canvas_look {

inline ImVec2 im(Vec2 v) { return ImVec2(v.x, v.y); }
inline Vec2 vec(ImVec2 v) { return Vec2{v.x, v.y}; }

// The colours of the old editor's theme: a title by the registry's family,
// the editor's own nodes their own.
constexpr ImU32 kFamilyColours[kFamilyCount] = {
  IM_COL32(95, 165, 150, 255),    // Oscillator: greenish teal
  IM_COL32(185, 95, 85, 255),     // Noise: reddish orange
  IM_COL32(155, 150, 110, 255),   // Wavetable: olive tan
  IM_COL32(195, 145, 90, 255),    // Additive: orange
  IM_COL32(95, 165, 150, 255),    // Physical: greenish teal
  IM_COL32(145, 130, 175, 255),   // Filter: dark lavender
  IM_COL32(200, 185, 110, 255),   // Envelope: pale gold
  IM_COL32(95, 165, 150, 255),    // Modulator: greenish teal
  IM_COL32(95, 165, 150, 255),    // Combiner: greenish teal
  IM_COL32(128, 128, 128, 255),   // Utility: grey
  IM_COL32(120, 130, 145, 255),   // Output: blue grey
};
constexpr ImU32 kOutputColour = IM_COL32(120, 130, 145, 255);   // the Output node
constexpr ImU32 kNoteColour = IM_COL32(205, 170, 60, 255);      // a note face: the only nodes the note feeds
constexpr ImU32 kCurveColour = IM_COL32(170, 145, 75, 255);     // a CurveNode, before its family's colour
constexpr ImU32 kUnknownColour = IM_COL32(128, 128, 128, 255);  // a type the registry does not know
constexpr ImU32 kDynamicColour = IM_COL32(205, 170, 60, 255);   // a setting's pin and its wire: set once a note
constexpr ImU32 kTapColour = IM_COL32(70, 150, 160, 255);       // the tap pin and its wires: the previous sample
constexpr ImU32 kTitleText = IM_COL32(20, 20, 24, 255);         // dark, readable on every title colour

// A node's title colour by its type.
inline ImU32 title_colour(const std::string& type) {
  if (type == "CurveNode") return kCurveColour;
  const SourceRegistry& reg = SourceRegistry::instance();
  if (!reg.has(type)) return kUnknownColour;
  return kFamilyColours[int(reg.family(type))];
}

// `c` blended a share of the way toward a dark grey: a node's body.
inline ImU32 body_colour(ImU32 c) {
  const auto mix = [](ImU32 v, int base) { return base + (int(v) - base) * 35 / 100; };
  return IM_COL32(mix(c & 0xFF, 45), mix((c >> 8) & 0xFF, 45), mix((c >> 16) & 0xFF, 48), 255);
}

// `c` made lighter by `by` in each channel: a hovered title or body.
inline ImU32 lighter(ImU32 c, int by) {
  const auto up = [&](ImU32 v) { return std::min(255, int(v) + by); };
  return IM_COL32(up(c & 0xFF), up((c >> 8) & 0xFF), up((c >> 16) & 0xFF), 255);
}

}  // namespace mforce::ui::canvas_look
