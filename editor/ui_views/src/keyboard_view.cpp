#include "mforce/ui_views/keyboard_view.h"
#include <algorithm>
#include <string_view>
#include <vector>
#include <imgui.h>
#include "mforce/ui_control/keyboard.h"
#include "mforce/ui_control/playing.h"

namespace mforce::ui {

namespace {

// The key the mouse plays with: apart from every ImGuiKey, which the
// computer's keys are named by.
constexpr int kMouseKey = 1 << 20;
constexpr int kOctaves = 3;
constexpr float kPianoHeight = 70.0f;
constexpr float kMeterFall = 0.92f;

// The ImGui key for a character of the note-key table; ImGuiKey_None for one
// it has no key for.
ImGuiKey key_for(char c) {
  if (c >= 'A' && c <= 'Z') return static_cast<ImGuiKey>(ImGuiKey_A + (c - 'A'));
  if (c >= '0' && c <= '9') return static_cast<ImGuiKey>(ImGuiKey_0 + (c - '0'));
  switch (c) {
    case ',': return ImGuiKey_Comma;
    case '.': return ImGuiKey_Period;
    case ';': return ImGuiKey_Semicolon;
    case '/': return ImGuiKey_Slash;
    case '[': return ImGuiKey_LeftBracket;
    case ']': return ImGuiKey_RightBracket;
    case '=': return ImGuiKey_Equal;
    default:  return ImGuiKey_None;
  }
}

// Every character that might be a note key; key_offset says which are.
constexpr std::string_view kCandidates = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789,.;/[]=";

bool is_black(int note) {
  const int pc = note % 12;
  return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

}  // namespace

void KeyboardView::computer_keys(Session& s) {
  const ImGuiIO& io = ImGui::GetIO();
  if (io.AppFocusLost) {
    release_all_keys(s);
    mouseNote_ = -1;
  }
  if (io.WantTextInput || io.KeyCtrl) return;
  for (char c : kCandidates) {
    const auto offset = key_offset(c);
    const ImGuiKey key = key_for(c);
    if (!offset || key == ImGuiKey_None) continue;
    if (ImGui::IsKeyPressed(key, false)) key_down(s, int(key), note_number(s.keyboard, *offset));
    if (ImGui::IsKeyReleased(key)) key_up(s, int(key));
  }
  if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) change_octave(s, 1);
  if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) change_octave(s, -1);
  if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) change_note_length(s, 0.5f);
  if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) change_note_length(s, 2.0f);
}

void KeyboardView::controls(Session& s) {
  ImGui::Text("octave %d", s.keyboard.octave);
  ImGui::SameLine();
  if (ImGui::SmallButton("-##octave")) change_octave(s, -1);
  ImGui::SameLine();
  if (ImGui::SmallButton("+##octave")) change_octave(s, 1);
  ImGui::SameLine();
  ImGui::Text("   note %.2f s", double(s.keyboard.seconds));
  ImGui::SameLine();
  if (ImGui::SmallButton("-##length")) change_note_length(s, 0.5f);
  ImGui::SameLine();
  if (ImGui::SmallButton("+##length")) change_note_length(s, 2.0f);
  ImGui::SameLine();
  meter(s);
}

void KeyboardView::meter(const Session& s) {
  meter_ = std::max(output_peak(s), meter_ * kMeterFall);
  ImGui::TextUnformatted("   peak");
  ImGui::SameLine();
  ImGui::ProgressBar(std::min(meter_, 1.0f), ImVec2(160.0f, 0.0f), "");
}

// Three octaves of keys, white then black on top; a held note is drawn
// pressed; a press of the mouse plays the key under it until it lets go.
void KeyboardView::piano(Session& s) {
  const int first = s.keyboard.octave * 12;
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const float width = std::max(ImGui::GetContentRegionAvail().x, 200.0f);
  const float white = width / float(7 * kOctaves);
  ImGui::InvisibleButton("piano", ImVec2(width, kPianoHeight));
  const std::vector<int> held = held_notes(s);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  int hit = -1;
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  for (int pass = 0; pass < 2; ++pass) {
    int whiteIndex = 0;
    for (int note = first; note < first + 12 * kOctaves; ++note) {
      const bool black = is_black(note);
      if (!black) ++whiteIndex;
      if (black != (pass == 1)) continue;
      const float x = black ? origin.x + white * float(whiteIndex) - white * 0.3f : origin.x + white * float(whiteIndex - 1);
      const ImVec2 lo(x, origin.y), hi(x + (black ? white * 0.6f : white), origin.y + kPianoHeight * (black ? 0.6f : 1.0f));
      const bool down = std::find(held.begin(), held.end(), note) != held.end();
      const ImU32 fill = down ? IM_COL32(90, 150, 230, 255) : (black ? IM_COL32(20, 20, 24, 255) : IM_COL32(225, 225, 225, 255));
      draw->AddRectFilled(lo, hi, fill);
      draw->AddRect(lo, hi, IM_COL32(60, 60, 60, 255));
      if (mouse.x >= lo.x && mouse.x < hi.x && mouse.y >= lo.y && mouse.y < hi.y) hit = note;
    }
  }
  if (ImGui::IsItemActivated() && hit >= 0 && hit <= 127) {
    mouseNote_ = hit;
    key_down(s, kMouseKey, hit);
  }
  if (ImGui::IsItemDeactivated() && mouseNote_ >= 0) {
    key_up(s, kMouseKey);
    mouseNote_ = -1;
  }
}

void KeyboardView::draw(Session& s) {
  computer_keys(s);
  controls(s);
  piano(s);
}

}  // namespace mforce::ui
