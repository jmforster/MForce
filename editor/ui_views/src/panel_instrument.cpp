// The properties panel with nothing selected: the instrument's own settings
// to edit, or a plain sound's facts.
#include <string>
#include <imgui.h>
#include "mforce/ui_control/building.h"
#include "mforce/ui_views/properties_panel.h"

namespace mforce::ui {

namespace {

constexpr float kFewestVoices = 1.0f, kMostVoices = 32.0f;
constexpr float kLongestGlideMs = 100.0f;

}  // namespace

void PropertiesPanel::patch_facts(const Session& s) {
  const Patch& p = s.open.patch;
  if (s.open.path.empty() && p.graph.nodes.empty()) {
    ImGui::TextUnformatted("No patch is open: File > Open (Ctrl+O).");
    return;
  }
  ImGui::TextUnformatted(s.open.path.empty() ? "(no file)" : s.open.path.c_str());
  ImGui::Text("rate %d Hz, %zu nodes", p.sampleRate, p.graph.nodes.size());
  ImGui::TextUnformatted("a plain sound");
}

// Each field applies once: a drag when it ends, the onsets when typed (names
// separated by commas), the checkbox at once.
void PropertiesPanel::instrument_fields(Session& s, const InstrumentBlock& block) {
  ImGui::TextUnformatted(s.open.path.empty() ? "(no file)" : s.open.path.c_str());
  ImGui::Text("an instrument, rate %d Hz", s.open.patch.sampleRate);
  ImGui::Separator();
  InstrumentBlock next = block;
  bool changed = false;
  float polyphony = float(block.polyphony);
  if (number_field("polyphony", "instrument/polyphony", polyphony, kFewestVoices, kMostVoices, true, polyphony)) {
    next.polyphony = int(polyphony);
    changed = true;
  }
  if (ImGui::Checkbox("sustaining", &next.sustaining)) changed = true;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Does this instrument hold a note past its written end? Phrasing, held lines and the\n"
                      "legato glide apply only when this is checked.");
  std::string onsets;
  if (text_field("onsets", "instrument/onsets", onsets_text(block.onsets), onsets)) {
    next.onsets = onsets_from_text(onsets);
    changed = true;
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("The onset names this patch answers to, separated by commas: tongue, slur");
  float glide = block.glideMs;
  if (number_field("glide ms", "instrument/glide", glide, 0.0f, kLongestGlideMs, false, glide)) {
    next.glideMs = glide;
    changed = true;
  }
  ImGui::Text("volume %g", double(block.volume));
  if (changed && !(next == block)) edit_instrument(s, next);
}

}  // namespace mforce::ui
