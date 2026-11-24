// The transport strip: its tabs, its buttons and the last note. Each tab's
// fields are in transport_fields.cpp.
#include "mforce/ui_views/transport_view.h"
#include <algorithm>
#include <string>
#include <imgui.h>
#include "mforce/ui_control/generating.h"

namespace mforce::ui {

namespace {

using Mode = Transport::Mode;

const ImVec4 kGeneratingColour(0.75f, 0.35f, 0.10f, 1.0f);
const ImVec4 kLastNoteColour(0.75f, 0.75f, 0.9f, 1.0f);

}  // namespace

TransportView::TransportView(FileDialogs& dialogs) : dialogs_(dialogs) {}

void TransportView::remember(const std::string& label) {
  const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
  centres_[label] = Vec2{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f};
}

std::optional<Vec2> TransportView::centre_of(const std::string& label) const {
  const auto it = centres_.find(label);
  if (it == centres_.end()) return std::nullopt;
  return it->second;
}

// The tab shown is the transport's mode: a tab clicked sets it, and a mode
// set elsewhere (the command line) selects its tab.
void TransportView::tabs(Session& s) {
  static constexpr struct { const char* label; Mode mode; } kTabs[] = {
      {"Note", Mode::Note}, {"Passage", Mode::Passage}, {"Chords", Mode::Chords}, {"Drums", Mode::Drums}};
  if (!ImGui::BeginTabBar("##transport_tabs")) return;
  for (const auto& tab : kTabs) {
    const bool shown = s.transport.mode == tab.mode;
    const ImGuiTabItemFlags flags = shown ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
    const bool open = ImGui::BeginTabItem(tab.label, nullptr, flags);
    remember(tab.label);
    if (ImGui::IsItemClicked() && !shown) {
      Transport t = s.transport;
      t.mode = tab.mode;
      set_transport(s, t);
    }
    if (open) ImGui::EndTabItem();
  }
  ImGui::EndTabBar();
}

// Generate, or "Generating..." in its colour once asked; Play, or Stop
// while anything plays; Save WAV.
void TransportView::buttons(Session& s) {
  if (generate_requested(s)) {
    ImGui::PushStyleColor(ImGuiCol_Button, kGeneratingColour);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kGeneratingColour);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kGeneratingColour);
    ImGui::Button("Generating...");
    ImGui::PopStyleColor(3);
    remember("Generating...");
  } else {
    if (ImGui::Button("Generate")) request_generate(s);
    remember("Generate");
  }
  ImGui::SameLine();
  if (playing(s)) {
    if (ImGui::Button("Stop")) stop(s);
    remember("Stop");
  } else {
    if (ImGui::Button("Play")) play(s);
    remember("Play");
  }
  ImGui::SameLine();
  if (ImGui::Button("Save WAV")) {
    const std::string path = dialogs_.pick_save(FileKind::Wav, {});
    if (!path.empty()) save_wav(s, path);
  }
  remember("Save WAV");
}

// The last note, right-aligned on the buttons' line.
void TransportView::last_note_text(const Session& s) {
  const std::string text = last_note(s);
  if (text.empty()) return;
  const float width = ImGui::CalcTextSize(text.c_str()).x;
  ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8.0f, ImGui::GetWindowContentRegionMax().x - width));
  ImGui::TextColored(kLastNoteColour, "%s", text.c_str());
}

// Save and Load for the tab's text, each through the kind's dialog.
void TransportView::text_buttons(Session& s, const char* save, const char* load, FileKind kind) {
  ImGui::SameLine(0, 12);
  if (ImGui::Button(save)) {
    const std::string path = dialogs_.pick_save(kind, {});
    if (!path.empty()) save_text(s, path);
  }
  ImGui::SameLine(0, 4);
  if (ImGui::Button(load)) {
    const std::string path = dialogs_.pick_open(kind);
    if (!path.empty()) load_text(s, path);
  }
}

void TransportView::draw(Session& s) {
  centres_.clear();                               // only what this frame draws
  tabs(s);
  switch (s.transport.mode) {
    case Mode::Note:    note_fields(s); break;
    case Mode::Passage: passage_fields(s); break;
    case Mode::Chords:  chords_fields(s); break;
    case Mode::Drums:   drums_fields(s); break;
  }
  ImGui::Separator();
  buttons(s);
  last_note_text(s);
}

}  // namespace mforce::ui
