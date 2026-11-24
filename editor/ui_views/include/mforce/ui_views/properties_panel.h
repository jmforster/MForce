#pragma once
#include <string>
#include "mforce/patch/node.h"
#include "mforce/patch/type_declarations.h"
#include "mforce/ui_control/session.h"

namespace mforce::ui {

// The panel down the right side. At its top a chooser of what the canvas
// draws (each node no face names, each face). Then, for a node: its name
// field, its type in its family's colour, its pins with their hints, its
// settings, each setting that can be driven once a note with a mark that
// offers its pin on the node. For a face: its name field and its six
// fields, with which are read. With nothing selected: the instrument's
// polyphony, sustaining, onset names and glide time to edit, its volume
// shown; for a plain sound, its facts.
//
// A pin's number follows the drag on every change (edit_value), and the
// drag is one undo step, ended when the field is let go. A float or int
// setting rebuilds the player, so its drag shows the number being dragged
// and applies once, when the drag ends or a typed number is entered; a
// checkbox and a choice apply at once; a text field, a name among them,
// applies on Enter or when it loses the keyboard.
class PropertiesPanel {
public:
  void draw(Session& s);

private:
  // properties_panel.cpp: the chooser, the name field, a face.
  void choose_node(Session& s);
  void name_field(Session& s, const std::string& name);
  void face_panel(Session& s, const NoteFace& face);
  // A text field holding `now`, keyed by `key`: true, with the text in `out`,
  // on the frame it applies.
  bool text_field(const char* label, const std::string& key, const std::string& now, std::string& out);
  // A number field dragged or typed, keyed by `key`: true, with the number in
  // `out`, on the frame it applies; `isInt` drags whole numbers.
  bool number_field(const char* label, const std::string& key, float now, float lo, float hi, bool isInt, float& out);
  // panel_node.cpp: a node.
  void node_panel(Session& s, const Node& n);
  void pins(Session& s, const Node& n, const TypeDeclarations& d);
  void pin_row(Session& s, const Node& n, const std::string& name, const std::string& hint, float lo, float hi,
               bool isParam);
  void settings(Session& s, const Node& n, const TypeDeclarations& d);
  void number_setting(Session& s, const Node& n, const TypeDeclarations::Setting& st, float v);
  void string_setting(Session& s, const Node& n, const TypeDeclarations::Setting& st);
  void offer_mark(Session& s, const Node& n, const TypeDeclarations::Setting& st);
  // panel_instrument.cpp: nothing selected.
  void patch_facts(const Session& s);
  void instrument_fields(Session& s, const InstrumentBlock& block);

  // Drawing state: the field being dragged or typed in and what it holds
  // until it applies. Not patch data.
  std::string draggedKey_;
  float dragged_{0.0f};
  std::string typedKey_;
  std::string typed_;
  int seenOpened_{-1};                             // session.opened when the state above was kept
};

}  // namespace mforce::ui
