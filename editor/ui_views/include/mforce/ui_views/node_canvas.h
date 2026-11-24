#pragma once
#include <string>
#include <vector>
#include "mforce/patch/layout.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_views/canvas_table.h"
#include "mforce/ui_views/create_menu.h"

namespace mforce::ui {

// The graph, drawn with imnodes from a CanvasTable made each frame: one
// generic node per graph node no face names, from its type's declarations;
// one node per note face; the canvas's Output node; the wires as links.
// Everything a person does here is one call of ui_control:
//   a drag between two pins          wire
//   a wire dragged off its pin       unwire
//   Delete or Backspace              delete_these (the selected wires, then the selected node or face)
//   a right click on empty canvas    the create menu: create_node or create_note at the click
//   a right click on a node or Note  its menu: Delete, delete_these with its name
//   a drag of nodes or faces         edit_position for each, then end_gesture when the mouse lets go
//   the wheel, Shift with an arrow   pan_canvas
//   F                                fit_canvas
// The record holds the positions, the panning and the selected node or
// face: each frame the canvas shows them. imnodes itself holds which wires
// are selected, as slots of its own pool, not by wire; so whenever the links
// the canvas draws differ from those it drew the frame before, that
// selection is cleared before the frame's gestures are read, and the Delete
// key never removes a wire nobody chose. The canvas keeps only drawing state:
// the names of imnodes' integers, last frame's links, the menu, where the
// create menu was opened.
class NodeCanvas {
public:
  void draw(Session& s);
  Vec2 size() const { return size_; }              // the canvas's size on screen, from the last frame
  Vec2 origin() const { return origin_; }          // its top-left corner on screen, from the last frame

private:
  // node_canvas.cpp: a frame.
  void show_record(const Session& s, const CanvasTable& t);
  void carry_moves(Session& s, const CanvasTable& t);
  void carry_selection(Session& s, const CanvasTable& t);
  // canvas_nodes.cpp: drawing.
  void draw_nodes(const Session& s, const CanvasTable& t);
  void draw_links(const CanvasTable& t);
  // canvas_gestures.cpp: what a person does.
  void carry_wiring(Session& s, const CanvasTable& t);
  void carry_keys(Session& s, const CanvasTable& t);
  void carry_wheel(Session& s);
  // canvas_menu.cpp: the create menu and a node's menu.
  void open_menus(const CanvasTable& t);
  void draw_menus(Session& s);

  CanvasNames names_;
  std::vector<CanvasLink> drawnLinks_;             // the links drawn last frame
  CreateMenu menu_ = create_menu();
  int seenOpened_{-1};                             // session.opened when names_ was begun
  bool gesture_{false};                            // a drag or a pan is under way: the canvas keeps the user's
  Vec2 shownPanning_{};                            // the panning given to imnodes this frame
  Vec2 origin_{};                                  // the canvas's top-left corner on screen
  Vec2 size_{};                                    // the canvas's size on screen
  Vec2 menuAt_{};                                  // where in the record the create menu was opened
  std::string menuNode_;                           // the node or Note whose menu is open
};

}  // namespace mforce::ui
