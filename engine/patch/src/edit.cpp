#include "mforce/patch/edit.h"
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "mforce/patch/type_declarations.h"
#include "edit_support.h"
#include "float_text.h"
#include "node_rules.h"
#include "sentence.h"

namespace mforce {

// The edits of a value, a setting, a string and a position: each tests the one
// thing it changes and writes it in place, because a drag calls them sixty
// times a second. edit_support.h holds the helpers they share with the
// structural edits.
using namespace edit_detail;   // the shared helpers by their short names (C#: a `using` directive)

Change set_value(Patch& p, std::string_view node, std::string_view pin, float v) {
  const NamedOnNode at = named_on_node(p, node, pin);
  const Pin shape = pin_of(at.d, pin);
  if (!shape.declared) throw std::runtime_error(at.head + " is not a pin of " + at.node.type);
  if (shape.slot) throw std::runtime_error(at.head + " is a slot: it takes a part, not a number");
  const std::string key(pin);
  const auto fed = at.node.wires.find(key);
  if (fed != at.node.wires.end() && !fed->second.empty())
    throw std::runtime_error(at.head + " is fed by " + in_quotes(fed->second.front().source) +
                             ": a pin takes a number or a feed, not both");
  if (!std::isfinite(v))
    throw std::runtime_error(at.head + " is " + float_text(v) + ": a pin's constant is a finite number");
  at.node.values[key] = v;
  return Change::Value;
}

Change set_setting(Patch& p, std::string_view node, std::string_view name, float v) {
  const NamedOnNode at = named_on_node(p, node, name);
  const TypeDeclarations::Setting* s = scalar_setting(at.d, name);
  if (!s) throw std::runtime_error(at.head + " is not a setting of " + at.node.type);
  const std::string is = at.head + " is " + float_text(v);
  if (!std::isfinite(v)) throw std::runtime_error(is + ": a setting holds a finite number");
  const std::vector<std::string> faults = setting_faults(*s, v);
  if (!faults.empty()) throw std::runtime_error(is + faults.front());
  at.node.settings[std::string(name)] = v;
  return Change::Structure;
}

Change set_string(Patch& p, std::string_view node, std::string_view name, std::string v) {
  const NamedOnNode at = named_on_node(p, node, name);
  if (!string_setting(at.d, name)) throw std::runtime_error(at.head + " is not a string setting of " + at.node.type);
  at.node.strings[std::string(name)] = std::move(v);
  return Change::Structure;
}

Change move_node(Patch& p, std::string_view node, Vec2 pos) {
  const Node& n = node_named(p, node, "there is nothing to move");
  require_finite(pos, node_opening(n) + "its position");
  p.layout.positions[n.id] = pos;
  return Change::Layout;
}

Change move_output(Patch& p, Vec2 pos) {
  require_finite(pos, "the Output node's position");
  p.layout.outputPos = pos;
  return Change::Layout;
}

Change set_panning(Patch& p, Vec2 pos) {
  require_finite(pos, "the canvas's panning");
  p.layout.panning = pos;
  return Change::Layout;
}

}  // namespace mforce
