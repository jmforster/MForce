#pragma once
#include <string>
#include <vector>
#include "mforce/patch/layout.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_views/canvas_table.h"
#include "mforce/ui_views/clipboard.h"
#include "mforce/ui_views/create_menu.h"
#include "mforce/ui_views/places.h"

namespace mforce::ui {

// One level of the graph (Session::groupPath), drawn with imnodes from a
// CanvasTable made each frame: one generic node per graph node no face
// names, from its type's declarations; a Wormhole's small face; one node per
// note face; one node per group shown, with a pin for each wire crossing its
// edge; at the top, the canvas's Output node; the wires as links. Everything
// a person does here is one call of ui_control:
//   a drag between two pins          wire (a group's pin stands for its real end)
//   a wire dragged off its pin       unwire
//   a click, Ctrl and a click, a box the selection (select_nodes, toggle_selected)
//   Delete or Backspace              delete_these (the selected wires, then each selected thing)
//   a double-click                   a group: open_group; a paired Wormhole: go_to its twin
//   a right click on a wire          route_wire
//   a right click on empty canvas    the create menu: Paste, then create_node or create_note at the click
//   a right click on a thing         its menu (canvas_menu.cpp); the thing alone selected first when it is not
//   a drag of what is selected       edit_position for each, then end_gesture when the mouse lets go
//   the wheel, Shift with an arrow   pan_canvas
//   F, wherever the mouse is         fit_canvas
//   Ctrl+X, Ctrl+C, Ctrl+V           cut_text, copy_text, paste_text with the clipboard
// While the left button is down imnodes owns the node selection; on the
// frame it is let go over the canvas the record takes it (the Output node
// left out); on every other frame imnodes is given the record's names shown
// here. imnodes itself holds which wires are selected, as slots of its own
// pool, not by wire; so whenever the links the canvas draws differ from
// those it drew the frame before, that selection is cleared before the
// frame's gestures are read, and the Delete key never removes a wire nobody
// chose. The canvas keeps only drawing state.
class NodeCanvas {
public:
  explicit NodeCanvas(Clipboard& clipboard);
  void draw(Session& s);
  Vec2 size() const { return size_; }              // the canvas's size on screen, from the last frame
  Vec2 origin() const { return origin_; }          // its top-left corner on screen, from the last frame
  // Where each thing was drawn last frame: "node:<name>" (a node, a face),
  // "group:<name>", "pin:<node>.<pin>" (a pin of a node or a face; a group's
  // as "pin:<group>.<label>"), "menu:<words>" for an item of its open menu,
  // and the listening mark, as "mark:<node>".
  const Places& places() const { return places_; }

private:
  // node_canvas.cpp: a frame.
  void show_record(const Session& s, const CanvasTable& t);
  void carry_moves(Session& s, const CanvasTable& t);
  void note_places(const CanvasTable& t);
  // canvas_select.cpp: the selection, a double-click.
  void carry_selection(Session& s, const CanvasTable& t);
  void carry_double_click(Session& s, const CanvasTable& t);
  // canvas_nodes.cpp: drawing.
  void draw_nodes(const Session& s, const CanvasTable& t);
  void draw_links(const CanvasTable& t);
  // canvas_wormholes.cpp: the ghost span and the tooltip.
  void draw_ghosts(const Session& s, const CanvasTable& t);
  void wormhole_tooltip(const Session& s, const CanvasTable& t);
  // canvas_gestures.cpp: what a person does.
  void carry_wiring(Session& s, const CanvasTable& t);
  void carry_keys(Session& s, const CanvasTable& t);
  void carry_wheel(Session& s);
  // canvas_menu.cpp and canvas_thing_menu.cpp: the create menu and a thing's menu.
  void open_menus(Session& s, const CanvasTable& t);
  void draw_menus(Session& s);
  void thing_menu(Session& s);
  void group_menu(Session& s);
  void create_items(Session& s);

  Clipboard& clipboard_;
  CanvasNames names_;
  std::vector<CanvasLink> drawnLinks_;             // the links drawn last frame
  CreateMenu menu_ = create_menu();
  int seenOpened_{-1};                             // session.opened when names_ was begun
  bool gesture_{false};                            // a drag or a pan is under way: the canvas keeps the user's
  bool overCanvas_{false};                         // the mouse is on the canvas itself this frame: not on a menu over it, nor the mini-map
  bool pressedHere_{false};                        // the left button went down on the canvas and is still down
  std::string ctrlPin_;                            // a Ctrl and a click on this thing's pin, to toggle when the button is let go
  bool ctrlDeselect_{false};                       // the Ctrl and a click under way takes a thing out
  std::string hoveredNode_;                        // the node the mouse was over last frame, by name
  Vec2 shownPanning_{};                            // the panning given to imnodes this frame
  Vec2 origin_{};                                  // the canvas's top-left corner on screen
  Vec2 size_{};                                    // the canvas's size on screen
  Vec2 menuAt_{};                                  // where in the record the create menu was opened
  std::string menuNode_;                           // the thing whose menu is open
  CanvasNode::Kind menuKind_{CanvasNode::Kind::Graph};   // and what it is
  Places places_;
};

}  // namespace mforce::ui
