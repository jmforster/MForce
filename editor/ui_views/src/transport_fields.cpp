// The transport's four tabs of fields. Each field reads the session, and a
// change is one call: set_transport for the transport's own fields, the
// keyboard's functions for the octave, velocity and note length it shares.
#include <algorithm>
#include <string>
#include <imgui.h>
#include <imgui_stdlib.h>
#include "mforce/ui_control/generating.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_views/transport_view.h"

namespace mforce::ui {

namespace {

constexpr float kSpinnerWidth = 96.0f;
constexpr float kNameWidth = 100.0f;

// A label just before the next field, a little apart from the one before.
void label(const char* text) {
  ImGui::SameLine(0, 12);
  ImGui::TextUnformatted(text);
  ImGui::SameLine(0, 4);
}

// A label that begins a row, level with the fields after it.
void lead(const char* text) {
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(text);
  ImGui::SameLine(0, 4);
}

// A number with step buttons, kept within [lo, hi]; true when it changed.
bool spinner(const char* id, float& v, float step, float lo, float hi, const char* format) {
  ImGui::SetNextItemWidth(kSpinnerWidth);
  float shown = v;
  if (!ImGui::InputFloat(id, &shown, step, step, format)) return false;
  v = std::clamp(shown, lo, hi);
  return true;
}

bool spinner(const char* id, int& v, int lo, int hi) {
  ImGui::SetNextItemWidth(kSpinnerWidth);
  int shown = v;
  if (!ImGui::InputInt(id, &shown, 1, 1)) return false;
  v = std::clamp(shown, lo, hi);
  return true;
}

// The two-line text of the Passage and Chords tabs, across the strip.
bool two_lines(const char* id, std::string& text) {
  const float height = ImGui::GetTextLineHeight() * 2.0f + ImGui::GetStyle().FramePadding.y * 2.0f + 6.0f;
  return ImGui::InputTextMultiline(id, &text, ImVec2(-1.0f, height));
}

void octave_field(Session& s, const char* id) {
  int octave = s.keyboard.octave;
  if (spinner(id, octave, 0, 8)) change_octave(s, octave - s.keyboard.octave);
}

void velocity_field(Session& s, const char* id) {
  float v = s.keyboard.velocity;
  if (spinner(id, v, 0.05f, 0.0f, 1.0f, "%.2f")) set_velocity(s, v);
}

void bpm_field(Session& s, const char* id) {
  Transport t = s.transport;
  if (spinner(id, t.bpm, 5.0f, 20.0f, 300.0f, "%.0f")) set_transport(s, t);
}

}  // namespace

// The pitch and octave compose the note's text; velocity; duration; Live.
void TransportView::note_fields(Session& s) {
  static constexpr const char* kPitch[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  const int number = note_tab_number(s);
  int pitch = ((number % 12) + 12) % 12, octave = std::clamp(number / 12, 0, 8);
  lead("Note");
  ImGui::SetNextItemWidth(56.0f);
  bool changed = ImGui::Combo("##notepc", &pitch, kPitch, 12);
  label("Octave");
  changed = spinner("##noct", octave, 0, 8) || changed;
  if (changed) set_note_tab(s, pitch, octave);
  label("Velocity");
  velocity_field(s, "##nvel");
  label("Duration");
  float seconds = s.keyboard.seconds;
  if (spinner("##dur", seconds, 0.1f, 0.1f, 30.0f, "%.1f")) set_note_length(s, seconds);
  ImGui::SameLine(0, 12);
  Transport t = s.transport;
  if (ImGui::Checkbox("Live", &t.live)) set_transport(s, t);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Checked: QWERTY and on-screen keys hold until\nkey/mouse release. Unchecked: both fire fixed\n"
                      "Duration notes (tests duration-aware patches).");
}

void TransportView::passage_fields(Session& s) {
  Transport t = s.transport;
  lead("Passage");
  if (two_lines("##passage", t.passage)) set_transport(s, t);
  lead("Octave");
  octave_field(s, "##poct");
  label("BPM");
  bpm_field(s, "##pbpm");
  label("Velocity");
  velocity_field(s, "##pvel");
  label("Onsets");
  t = s.transport;
  int onsets = int(t.onsets);
  ImGui::SetNextItemWidth(90.0f);
  if (ImGui::Combo("##onsets", &onsets, "auto\0all tongue\0all slur\0all breath\0")) {
    t.onsets = Onsets(onsets);
    set_transport(s, t);
  }
  text_buttons(s, "Save Passage", "Load Passage", FileKind::Passage);
}

void TransportView::chords_fields(Session& s) {
  Transport t = s.transport;
  lead("Chords");
  if (two_lines("##chords", t.chords)) set_transport(s, t);
  lead("Group");
  t = s.transport;
  ImGui::SetNextItemWidth(kNameWidth);
  if (ImGui::InputText("##grp", &t.chordGroup)) set_transport(s, t);
  label("Figure");
  t = s.transport;
  ImGui::SetNextItemWidth(kNameWidth);
  if (ImGui::InputText("##fig", &t.figure)) set_transport(s, t);
  text_buttons(s, "Save Chords", "Load Chords", FileKind::Chords);
  lead("Octave");
  octave_field(s, "##coct");
  label("BPM");
  bpm_field(s, "##cbpm");
  label("Delay");
  t = s.transport;
  if (spinner("##cdly", t.delayMs, 5.0f, 0.0f, 200.0f, "%.0f")) set_transport(s, t);
}

void TransportView::drums_fields(Session& s) {
  Transport t = s.transport;
  lead("Pattern");
  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::InputText("##pat", &t.drumPattern)) set_transport(s, t);
  lead("Drums");
  t = s.transport;
  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::InputText("##dmap", &t.drumMap)) set_transport(s, t);
  lead("BPM");
  bpm_field(s, "##dbpm");
}

}  // namespace mforce::ui
