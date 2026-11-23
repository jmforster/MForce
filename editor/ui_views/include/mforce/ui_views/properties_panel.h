#pragma once
#include <array>
#include <string>
#include "mforce/patch/node.h"
#include "mforce/patch/type_declarations.h"
#include "mforce/ui_control/session.h"

namespace mforce::ui {

// The selected node's pins and settings, from its declarations and the
// record; with nothing selected, the patch's own facts, read only.
//
// A pin's number follows the drag on every change (edit_value), so a held
// note follows it. A float or int setting rebuilds the player, so its drag
// shows the number being dragged and applies once, when the drag ends or a
// typed number is entered; a checkbox and a choice apply at once; a text
// field applies on Enter or when it loses the keyboard.
class PropertiesPanel {
public:
  void draw(Session& s);

private:
  void patch_facts(const Session& s);
  void pins(Session& s, const Node& n, const TypeDeclarations& d);
  void pin_row(Session& s, const Node& n, const std::string& name, float lo, float hi, bool isParam);
  void settings(Session& s, const Node& n, const TypeDeclarations& d);
  void number_setting(Session& s, const Node& n, const TypeDeclarations::Setting& st, float v);
  void string_setting(Session& s, const Node& n, const TypeDeclarations::Setting& st);
  void arrays(const Node& n, const TypeDeclarations& d);

  // Drawing state: the field being dragged or typed in and what it holds
  // until it applies. Not patch data.
  std::string draggedKey_;
  float dragged_{0.0f};
  std::string typedKey_;
  std::array<char, 256> typed_{};
};

}  // namespace mforce::ui
