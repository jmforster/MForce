#pragma once
#include <map>
#include <string>
#include "mforce/patch/layout.h"
#include "mforce/ui_control/session.h"

namespace mforce::ui {

// The graph, drawn with imnodes: one generic node per graph node from its
// type's declarations, the wires as links (a tap and a dynamic pin each in a
// colour of its own), and the canvas's Output node fed from graph.output.
// The record holds the positions, the panning and the selection: each frame
// the canvas shows them, and a drag that ends is carried to the record by
// one edit. Nothing is wired or unwired here.
//
// imnodes names nodes, pins and links by integers. A graph node's integer is
// given the first time its id is drawn and kept for the life of the canvas,
// never reused (0 is the Output node's); a pin is its node's integer times
// 1024 plus its row (0 the output, 1 on the input rows in order); a link is
// its reading pin's integer times 256 plus the feed's place in the pin's list.
class NodeCanvas {
public:
  void draw(Session& s);

private:
  int node_int(const std::string& id);             // the node's imnodes integer, given on first use
  std::string id_of(int nodeInt) const;            // empty for the Output node or an unknown integer
  void show_record(const Session& s);
  void draw_nodes(const Session& s);
  void draw_output(const Session& s);
  void draw_links(const Session& s);
  void carry_moves(Session& s);
  void carry_selection(Session& s);

  std::map<std::string, int> ints_;                // node id -> imnodes integer
  int nextInt_{1};
  bool gesture_{false};                            // a drag or a pan is under way: the canvas keeps the user's
  Vec2 shownPanning_{};                            // the panning given to imnodes this frame
};

}  // namespace mforce::ui
