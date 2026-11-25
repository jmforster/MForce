// A thing's menu and a group's menu on the canvas.
#include <algorithm>
#include <string>
#include <vector>
#include <imgui.h>
#include "mforce/ui_control/building.h"
#include "mforce/ui_control/clipboard.h"
#include "mforce/ui_control/grouping.h"
#include "mforce/ui_control/listening.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_model/groups.h"
#include "mforce/ui_views/node_canvas.h"
#include "canvas_popups.h"

namespace mforce::ui {

namespace {

// What a thing's menu chose, done once the menu is closed.
struct Chosen {
  bool duplicate{false}, copy{false}, remove{false}, listen{false}, group{false}, removeFrom{false};
  std::string replaceWith, addTo;
};

bool is_group(const Patch& p, const std::string& name) {
  return std::any_of(p.layout.groups.begin(), p.layout.groups.end(), [&](const Group& g) { return g.name == name; });
}

// The selection's nodes and faces: what joins and leaves groups.
std::vector<std::string> targets(const Session& s) {
  std::vector<std::string> names;
  for (const auto& n : s.selections)
    if (!is_group(s.open.patch, n)) names.push_back(n);
  return names;
}

std::string type_of(const Patch& p, const std::string& id) {
  for (const auto& n : p.graph.nodes)
    if (n.id == id) return n.type;
  return {};
}

// "Replace with" and its items: the favourites that make a node, then the
// families and the Experimental shelf; the type chosen, or empty.
std::string replace_menu(const CreateMenu& menu, Places& places) {
  std::string type;
  const bool open = ImGui::BeginMenu("Replace with");
  places.note_item("menu:Replace with");
  if (!open) return type;
  for (const auto& item : menu.favourites) {
    if (item.type.empty()) continue;
    if (ImGui::MenuItem(item.label.c_str())) type = item.type;
    places.note_item("menu:" + item.label);
  }
  ImGui::Separator();
  if (const CreateItem* c = canvas_popups::families_menu(menu.main)) type = c->type;
  if (!menu.experimental.empty()) {
    ImGui::Separator();
    if (ImGui::BeginMenu("Experimental")) {
      if (const CreateItem* c = canvas_popups::families_menu(menu.experimental)) type = c->type;
      ImGui::EndMenu();
    }
  }
  ImGui::EndMenu();
  return type;
}

// "Group selection (n)", "Add to group (n)" with every group not all the
// targets are in, "Remove from '<group>'" when the thing is in one.
void group_items(const Session& s, const std::string& thing, Chosen& c, Places& places) {
  const std::string selected = std::to_string(s.selections.size());
  c.group = ImGui::MenuItem(("Group selection (" + selected + ")").c_str(), nullptr, false, !s.selections.empty());
  places.note_item("menu:Group selection (" + selected + ")");
  const std::vector<std::string> moving = targets(s);
  if (!s.open.patch.layout.groups.empty()) {
    const std::string words = "Add to group (" + std::to_string(moving.size()) + ")";
    const bool open = ImGui::BeginMenu(words.c_str());
    places.note_item("menu:" + words);
    if (open) {
      for (const auto& g : s.open.patch.layout.groups) {
        const bool allIn = std::all_of(moving.begin(), moving.end(), [&](const std::string& m) {
          return std::find(g.members.begin(), g.members.end(), m) != g.members.end();
        });
        if (allIn) continue;
        if (ImGui::MenuItem(g.name.c_str())) c.addTo = g.name;
        places.note_item("menu:" + g.name);
      }
      ImGui::EndMenu();
    }
  }
  const std::string holder = group_holding(s.open.patch, thing);
  if (holder.empty()) return;
  const std::string words = "Remove from '" + holder + "'";
  c.removeFrom = ImGui::MenuItem(words.c_str());
  places.note_item("menu:" + words);
}

// The items of a node's or a face's menu, in order.
Chosen thing_items(const Session& s, const std::string& thing, bool face, const CreateMenu& menu, Places& places) {
  Chosen c;
  const std::string type = type_of(s.open.patch, thing);
  const bool copies = !face && type != "SoundChannel" && type != "StereoMixer";
  if (copies) {
    c.duplicate = ImGui::MenuItem("Duplicate");
    places.note_item("menu:Duplicate");
  }
  const std::string copy = "Copy (" + std::to_string(std::max<std::size_t>(s.selections.size(), 1)) + ")";
  c.copy = ImGui::MenuItem(copy.c_str(), "Ctrl+C");
  places.note_item("menu:" + copy);
  if (copies) c.replaceWith = replace_menu(menu, places);
  c.remove = ImGui::MenuItem("Delete");
  places.note_item("menu:Delete");
  ImGui::Separator();
  if (!face) {
    const char* words = s.listening == thing ? "Stop listening here" : "Listen here";
    c.listen = ImGui::MenuItem(words);
    places.note_item(std::string("menu:") + words);
  }
  group_items(s, thing, c, places);
  return c;
}

}  // namespace

void NodeCanvas::thing_menu(Session& s) {
  const std::string thing = menuNode_;
  const Chosen c = thing_items(s, thing, menuKind_ == CanvasNode::Kind::Face, menu_, places_);
  ImGui::EndPopup();
  if (c.duplicate) duplicate(s, thing);
  else if (c.copy) {
    if (const std::string text = copy_text(s); !text.empty()) clipboard_.set_text(text);
  } else if (!c.replaceWith.empty()) replace_with(s, thing, c.replaceWith);
  else if (c.remove) delete_these(s, {}, {thing});
  else if (c.listen && s.listening == thing) stop_listening(s);
  else if (c.listen) listen_at(s, thing);
  else if (c.group) group_selection(s);
  else if (!c.addTo.empty()) add_to_group(s, c.addTo);
  else if (c.removeFrom) remove_from_group(s);
}

// A group's menu: the Listen item (when the group has an output), Open, Ungroup.
void NodeCanvas::group_menu(Session& s) {
  const std::string group = menuNode_;
  const std::string out = output_node(s.open.patch, group);
  const bool tapped = !out.empty() && s.listening == out;
  const char* words = tapped ? "Stop listening here" : "Listen here";
  const bool listen = ImGui::MenuItem(words, nullptr, false, !out.empty());
  places_.note_item(std::string("menu:") + words);
  const bool open = ImGui::MenuItem("Open");
  places_.note_item("menu:Open");
  const bool apart = ImGui::MenuItem("Ungroup");
  places_.note_item("menu:Ungroup");
  ImGui::EndPopup();
  if (listen && tapped) stop_listening(s);
  else if (listen) listen_at_group(s, group);
  else if (open) open_group(s, group);
  else if (apart) ungroup(s, group);
}

}  // namespace mforce::ui
