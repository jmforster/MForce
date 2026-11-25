#pragma once
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "mforce/patch/patch.h"
#include "mforce/ui_control/wiring.h"

namespace mforce::ui {

// What the canvas draws, as plain data made from the patch and the offered
// setting pins, with no ImGui in it: the canvas makes one each frame and
// draws from it, and decodes what imnodes reports through it.
//
// imnodes names nodes, pins and links by integers. CanvasNames gives them:
// a node's integer is given the first time its name is seen and kept while
// the canvas lives (0 is the Output node's); a pin's is its node's times
// 1024 plus its row; a link's is given the first time its taking end and
// feed number are seen, and kept, so it is the same from frame to frame
// while the patch does not change.
class CanvasNames {
public:
  int node(const std::string& key);                // "n:" + a node id, "f:" + a face label, "g:" + a group name
  int link(const std::string& key);                // the taking end and the feed's number
  static constexpr int kOutputNode = 0;
  static constexpr int kPinsPerNode = 1024;

private:
  std::map<std::string, int> nodes_;
  std::map<std::string, int> links_;
  int nextNode_{1};
  int nextLink_{1};
};

// One pin of a drawn node.
struct CanvasPin {
  enum class Look { Param, InputOnly, Setting, Output, Tap, Field, PatchOutput };
  int id{0};
  PinEnd end;
  Look look{Look::Param};
  std::string label;            // the pin's, setting's or field's name; "out", "tap", or the output's source;
                                // on a group, "member.pin" in and "out" or the member's name out
  bool fed{false};              // a feed, a drive or the output reaches it, drawn here or not
  std::string driver;           // a setting's pin: the node that drives it; empty when nothing does
  // What feeds it from outside the level, whose wire is not drawn here
  // ("Damp_lpf", "WhiteNoise (tap)", several joined by ", "); empty when none.
  std::string outside;
};

// What hovering a setting's pin on the canvas says: the setting set once a
// note by its driver, or that nothing drives it yet; empty for any other pin.
std::string pin_tooltip(const CanvasPin& pin);

// One thing drawn as a node: a graph node no face names, a face, a group
// shown at this level, or the canvas's Output node (at the top only). A
// group's pins hold the real ends they stand for: a member's pin it reads
// through, and the member (its output or its tap) read from outside.
struct CanvasNode {
  enum class Kind { Graph, Face, Group, Output };
  int id{0};
  Kind kind{Kind::Graph};
  std::string name;             // the node's id or the face's label; empty for the Output node
  std::string type;             // the node's type; empty for a face and the Output node
  std::vector<std::string> texts;   // each non-empty string setting's text, for the body
  int under{0};                 // a group: the nodes under it, a face counted once
  std::vector<CanvasPin> inputs;
  std::vector<CanvasPin> outputs;
};

// One wire: from a giving pin to a taking pin, and what unwire needs.
struct CanvasLink {
  enum class Look { Plain, Tap, Dynamic };
  int id{0};
  int from{0};
  int to{0};
  PinEnd reader;                // the taking end
  std::size_t feed{0};          // the feed's number on its pin
  Look look{Look::Plain};
};

struct CanvasTable {
  std::vector<CanvasNode> nodes;   // shown_at's nodes, faces and groups in its order, then the Output node at the top
  std::vector<CanvasLink> links;   // each feed, each dynamic pin, the output
  // The pin a pin's integer names; null for one the table does not have.
  const PinEnd* pin(int id) const;
  // The link a link's integer names; null for one the table does not have.
  const CanvasLink* link(int id) const;
  // The drawn node a node's integer names; null for one the table does not have.
  const CanvasNode* node(int id) const;
};

// The table for the level `path` of `patch` (shown_at) with the setting
// pins `offered` shown. A graph node's inputs are each declared Param, each
// declared Input, then each setting on offer or fed, in the declarations'
// order; its outputs its Output pin and, on a signal that is no Wormhole,
// its Tap pin. A face's outputs are the note's six fields; the Output
// node's one input is the PatchOutput. A group's inputs are its boundary's
// crossings in, its outputs one a member read from outside (one labelled
// "out" when it is the only one and plain). Each feed, drive and the output
// is a link between the pins that stand for its two ends at this level: a
// face's field for a face's node, a group's pin for what is inside it, a
// Wormhole's output for its tap; a feed with an end nothing here stands
// for, or with both ends in one group, and the span between two Wormholes,
// are not drawn. A pin drawn here that such a feed reaches from outside the
// level is still fed, and names that source (CanvasPin::outside).
CanvasTable canvas_table(const Patch& patch, const std::vector<std::string>& path,
                         const std::set<std::pair<std::string, std::string>>& offered, CanvasNames& names);

}  // namespace mforce::ui
