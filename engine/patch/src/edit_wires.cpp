// The edits of what reads what: a pin's feeds, a setting's dynamic pin, and
// the patch's output.
#include "mforce/patch/edit.h"
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>
#include "mforce/patch/type_declarations.h"
#include "edit_support.h"
#include "node_rules.h"
#include "sentence.h"

namespace mforce {

using namespace edit_detail;   // the shared helpers by their short names (C#: a `using` directive)

Change connect(Patch& p, std::string_view node, std::string_view pin, std::string_view source, bool tap) {
  Patch copy = p;
  const NamedOnNode at = named_on_node(copy, node, pin);
  const Pin shape = pin_of(at.d, pin);
  if (!shape.declared) throw std::runtime_error(at.head + " is not a pin of " + at.node.type);
  const std::string key(pin);
  std::vector<Wire>& feeds = at.node.wires[key];
  std::vector<std::string> replaced;
  if (!shape.list) {
    for (const auto& w : feeds) replaced.push_back(w.source);
    feeds.clear();
  }
  feeds.push_back(Wire{std::string(source), tap});
  at.node.values.erase(key);
  drop_unread_face_nodes(copy, replaced);
  return adopt(p, copy, Change::Structure);
}

Change disconnect(Patch& p, std::string_view node, std::string_view pin, std::size_t feed) {
  Patch copy = p;
  const NamedOnNode at = named_on_node(copy, node, pin);
  if (!pin_of(at.d, pin).declared) throw std::runtime_error(at.head + " is not a pin of " + at.node.type);
  const std::string key(pin);
  const auto fed = at.node.wires.find(key);
  const std::size_t count = fed == at.node.wires.end() ? 0 : fed->second.size();
  if (feed >= count)
    throw std::runtime_error(at.head + " has no feed number " + std::to_string(feed) + "; it has " +
                             std::to_string(count));
  const std::string gone = fed->second[feed].source;
  fed->second.erase(fed->second.begin() + std::ptrdiff_t(feed));
  if (fed->second.empty()) pin_lost_its_feeds(at.node, key);
  drop_unread_face_nodes(copy, {gone});
  return adopt(p, copy, Change::Structure);
}

Change feed_setting(Patch& p, std::string_view node, std::string_view setting, std::string_view source) {
  Patch copy = p;
  const NamedOnNode at = named_on_node(copy, node, setting);
  if (!at.d.setting(setting)) throw std::runtime_error(at.head + " is not a setting of " + at.node.type);
  std::vector<std::string> replaced;
  const std::string key(setting);
  if (auto old = at.node.dynamicPins.find(key); old != at.node.dynamicPins.end()) replaced.push_back(old->second);
  at.node.dynamicPins[key] = std::string(source);
  drop_unread_face_nodes(copy, replaced);
  return adopt(p, copy, Change::Structure);
}

Change unfeed_setting(Patch& p, std::string_view node, std::string_view setting) {
  Patch copy = p;
  Node& n = node_named(copy, node, "it has no dynamic pin " + in_quotes(setting));
  const auto pin = n.dynamicPins.find(std::string(setting));
  if (pin == n.dynamicPins.end())
    throw std::runtime_error(node_opening(n) + in_quotes(setting) + " has no dynamic pin to remove");
  const std::string gone = pin->second;
  n.dynamicPins.erase(pin);
  drop_unread_face_nodes(copy, {gone});
  return adopt(p, copy, Change::Structure);
}

Change set_output(Patch& p, std::string_view node) {
  Patch copy = p;
  const std::string old = copy.graph.output;
  copy.graph.output = std::string(node);
  if (old != node) drop_unread_face_nodes(copy, {old});
  return adopt(p, copy, Change::Structure);
}

} // namespace mforce
