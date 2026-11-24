#include "mforce/ui_views/keyboard_view.h"
#include <algorithm>
#include <string>
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
constexpr float kPianoHeight = 70.0f;
constexpr float kBlackShare = 0.65f;               // a black key's width, of a white key's
constexpr float kBlackDepth = 0.6f;                // and its depth, of the piano's
constexpr float kMeterFall = 0.92f;
constexpr int kWhiteSteps[7] = {0, 2, 4, 5, 7, 9, 11};
constexpr int kBlackAfter[5][2] = {{0, 1}, {1, 3}, {3, 6}, {4, 8}, {5, 10}};   // (white key it follows, semitone)

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

ImU32 band_colour(LevelBand band) {
  switch (band) {
    case LevelBand::Red:    return IM_COL32(255, 77, 77, 255);
    case LevelBand::Yellow: return IM_COL32(255, 230, 77, 255);
    case LevelBand::Green:  break;
  }
  return IM_COL32(102, 255, 102, 255);
}

// One key of the piano: where it is, its note, and whether it is black.
struct PianoKey {
  ImVec2 lo, hi;
  int note;
  bool black;
};

// The piano's keys for `octaves` octaves and a top C from `first`, in `width`
// at `origin`: the white keys first, then the black ones on top.
std::vector<PianoKey> piano_keys(ImVec2 origin, float width, int first, int octaves) {
  std::vector<PianoKey> keys;
  const int whites = octaves * 7 + 1;
  const float w = width / float(whites), bw = w * kBlackShare;
  for (int k = 0; k < whites; ++k)
    keys.push_back({ImVec2(origin.x + w * float(k), origin.y), ImVec2(origin.x + w * float(k + 1) - 1.0f, origin.y + kPianoHeight),
                    first + 12 * (k / 7) + kWhiteSteps[k % 7], false});
  for (int o = 0; o < octaves; ++o)
    for (const auto& [after, step] : kBlackAfter) {
      const float x = origin.x + w * float(o * 7 + after + 1) - bw * 0.5f;
      keys.push_back({ImVec2(x, origin.y), ImVec2(x + bw, origin.y + kPianoHeight * kBlackDepth), first + 12 * o + step, true});
    }
  return keys;
}

// The key under `p`, black keys first; null for none.
const PianoKey* key_at(const std::vector<PianoKey>& keys, ImVec2 p) {
  const PianoKey* hit = nullptr;
  for (const auto& k : keys)
    if (p.x >= k.lo.x && p.x < k.hi.x && p.y >= k.lo.y && p.y < k.hi.y && (!hit || k.black)) hit = &k;
  return hit;
}

void key_labels(ImDrawList* draw, const PianoKey& k, int first, bool computerKeys) {
  const float width = k.hi.x - k.lo.x;
  if (!k.black) {
    const std::string name = note_name(k.note);
    const ImVec2 size = ImGui::CalcTextSize(name.c_str());
    draw->AddText(ImVec2(k.lo.x + (width - size.x) * 0.5f, k.hi.y - size.y - 3.0f), IM_COL32(60, 60, 60, 255), name.c_str());
  }
  const std::string label = computerKeys ? key_label(k.note - first) : std::string();
  if (label.empty()) return;
  const ImVec2 size = ImGui::CalcTextSize(label.c_str());
  draw->AddText(ImVec2(k.lo.x + (width - size.x) * 0.5f, k.lo.y + 3.0f),
                k.black ? IM_COL32(200, 200, 100, 255) : IM_COL32(0, 0, 0, 255), label.c_str());
}

}  // namespace

void KeyboardView::computer_keys(Session& s) {
  const ImGuiIO& io = ImGui::GetIO();
  if (io.AppFocusLost) {
    release_all_keys(s);
    mouseNote_ = -1;
  }
  std::vector<KeyChange> changes;
  for (char c : kCandidates) {
    const ImGuiKey key = key_for(c);
    const bool pressed = ImGui::IsKeyPressed(key, false), released = ImGui::IsKeyReleased(key);
    if (pressed || released) changes.push_back({c, pressed, released});
  }
  for (const KeyAct& act : key_acts(s.keyboard, changes, io.WantTextInput || io.KeyCtrl)) {
    if (act.down) key_down(s, int(key_for(act.key)), act.noteNumber);
    else key_up(s, int(key_for(act.key)));
  }
  if (io.WantTextInput || io.KeyCtrl || io.KeyShift || !s.keyboard.computerKeys) return;   // Shift with an arrow is the canvas's pan
  if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) change_octave(s, 1);
  if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) change_octave(s, -1);
  if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) change_note_length(s, 0.5f);
  if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) change_note_length(s, 2.0f);
}

void KeyboardView::controls(Session& s) {
  const bool on = s.keyboard.computerKeys;
  if (on) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.6f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.7f, 0.4f, 1.0f));
  }
  if (ImGui::SmallButton("PC Keyboard")) toggle_computer_keys(s);
  if (on) ImGui::PopStyleColor(2);
  ImGui::SameLine();
  ImGui::Text("   octave %d", s.keyboard.octave);
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

void KeyboardView::meter(Session& s) {
  meter_ = std::max(output_peak(s), meter_ * kMeterFall);
  ImGui::Text("   voices=%d  peak", sounding_voices(s));
  ImGui::SameLine();
  ImGui::PushStyleColor(ImGuiCol_PlotHistogram, band_colour(level_band(meter_)));
  ImGui::ProgressBar(std::min(meter_, 1.0f), ImVec2(160.0f, 0.0f), "");
  ImGui::PopStyleColor();
}

// As many octaves as the room keeps a white key 25 to 45 points wide, from
// the keyboard octave to a top C; a held note is drawn pressed; a press of
// the mouse plays the key under it, louder further down the key, until the
// mouse lets go anywhere.
void KeyboardView::piano(Session& s) {
  const int first = s.keyboard.octave * 12;
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const float width = std::max(ImGui::GetContentRegionAvail().x, 200.0f);
  ImGui::InvisibleButton("piano", ImVec2(width, kPianoHeight));
  const std::vector<PianoKey> keys = piano_keys(origin, width, first, piano_octaves(width));
  const std::vector<int> held = held_notes(s);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  for (const auto& k : keys) {
    const bool down = std::find(held.begin(), held.end(), k.note) != held.end();
    const ImU32 fill = down ? IM_COL32(90, 150, 230, 255) : (k.black ? IM_COL32(20, 20, 24, 255) : IM_COL32(225, 225, 225, 255));
    draw->AddRectFilled(k.lo, k.hi, fill);
    draw->AddRect(k.lo, k.hi, IM_COL32(60, 60, 60, 255));
    key_labels(draw, k, first, s.keyboard.computerKeys);
  }
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  const PianoKey* hit = key_at(keys, mouse);
  if (ImGui::IsItemActivated() && hit && hit->note >= 0 && hit->note <= 127) {
    mouseNote_ = hit->note;
    key_down(s, kMouseKey, hit->note, velocity_at((mouse.y - hit->lo.y) / (hit->hi.y - hit->lo.y)));
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
