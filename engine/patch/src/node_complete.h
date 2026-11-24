#pragma once
#include "mforce/patch/node.h"
#include "mforce/patch/type_declarations.h"

namespace mforce {

// The model is complete in pins and settings: a Param with neither a number
// nor a feed takes its declared default, a setting the file left out its
// default, a string setting its default text. Inputs and arrays take nothing.
// The reader completes every node it reads with this, and add_node every node
// it makes, so a new node holds what a file's node holds.
inline void complete(Node& n, const TypeDeclarations& d) {
  for (const auto& p : d.params)
    if (!n.values.count(p.name) && !n.wires.count(p.name)) n.values[p.name] = p.defaultValue;
  for (const auto& s : d.settings) {
    if (s.type == SettingType::String) {
      if (!n.strings.count(s.name)) n.strings[s.name] = s.defaultText;
    } else if (!n.settings.count(s.name)) {
      n.settings[s.name] = s.defaultValue;
    }
  }
}

} // namespace mforce
