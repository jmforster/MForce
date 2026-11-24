#pragma once
#include <string>
#include <vector>

namespace mforce::ui {

// What the canvas's create menu offers, from the registry, as plain data.
struct CreateItem {
  std::string label;            // as the menu shows it
  std::string type;             // the registered type it makes; empty for the Note
};
struct CreateFamily {
  std::string name;             // the family's name ("Oscillators")
  std::vector<CreateItem> items;
  bool separatorBefore{false};  // the shaping families start here
};
struct CreateMenu {
  std::vector<CreateItem> favourites;    // Note, then Curve, Envelope, Var, Range
  std::vector<CreateFamily> main;        // the main shelf, by family in the registry's menu order
  std::vector<CreateFamily> experimental;
};

// Every registered type a person may make appears once: a favourite is not
// repeated in its family. Not offered: an Output, a Channel or a Mixer (the
// Output family: the Output node is always on the canvas), and a PerformNode
// by its own name (Note is how a person asks for one). A family with nothing
// to offer is left out.
CreateMenu create_menu();

}  // namespace mforce::ui
