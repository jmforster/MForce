#include "edit_support.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include "float_text.h"
#include "node_rules.h"
#include "sentence.h"

namespace mforce::edit_detail {

namespace {

// The ids `n` reads: its feeds, taps among them, and its dynamic pins' sources.
std::vector<std::string> reads_of(const Node& n) {
  std::vector<std::string> ids;
  for (const auto& [pin, feeds] : n.wires)
    for (const auto& w : feeds) ids.push_back(w.source);
  for (const auto& [setting, source] : n.dynamicPins) ids.push_back(source);
  return ids;
}

// True when a feed, a dynamic pin or the output reads `id`.
bool is_read(const Patch& p, const std::string& id) {
  if (p.graph.output == id) return true;
  for (const auto& n : p.graph.nodes) {
    for (const auto& [pin, feeds] : n.wires)
      for (const auto& w : feeds)
        if (w.source == id) return true;
    for (const auto& [setting, source] : n.dynamicPins)
      if (source == id) return true;
  }
  return false;
}

bool face_names(const Patch& p, const std::string& id) {
  for (const auto& f : p.layout.faces)
    for (const auto& [field, node] : f.fields)
      if (node == id) return true;
  return false;
}

// Every feed of `n` that reads `id` goes; a pin left with none loses it.
void drop_feeds_from(Node& n, const std::string& id) {
  std::vector<std::string> emptied;
  for (auto& [pin, feeds] : n.wires) {
    std::erase_if(feeds, [&](const Wire& w) { return w.source == id; });
    if (feeds.empty()) emptied.push_back(pin);
  }
  for (const auto& pin : emptied) pin_lost_its_feeds(n, pin);
  std::erase_if(n.dynamicPins, [&](const auto& pin) { return pin.second == id; });
}

}  // namespace

Node& node_named(Patch& p, std::string_view id, const std::string& after) {
  for (auto& n : p.graph.nodes)
    if (n.id == id) return n;
  throw std::runtime_error(in_quotes(id) + " names no node: " + after);
}

NamedOnNode named_on_node(Patch& p, std::string_view id, std::string_view name) {
  Node& n = node_named(p, id, "it has no " + in_quotes(name));
  const TypeDeclarations* d = type_declarations(n.type);
  if (!d)
    throw std::runtime_error(node_opening(n) + unknown_type_sentence(n.type) + "; so " + in_quotes(name) +
                             " has no declaration to check it against");
  return {n, *d, node_opening(n) + in_quotes(name)};
}

void require_finite(Vec2 pos, const std::string& what) {
  if (std::isfinite(pos.x) && std::isfinite(pos.y)) return;
  throw std::runtime_error(what + " (" + float_text(pos.x) + ", " + float_text(pos.y) +
                           ") is not two finite numbers");
}

std::string check_sentence(const std::string& line) {
  const auto cut = line.find(": ");
  return cut == std::string::npos ? line : line.substr(cut + 2);
}

Change adopt(Patch& p, Patch& copy, Change kind) {
  const std::vector<std::string> lines = copy.check();
  if (!lines.empty()) throw std::runtime_error(check_sentence(lines.front()));
  p = std::move(copy);
  return kind;
}

std::optional<std::size_t> index_of(const Patch& p, std::string_view id) {
  const auto& nodes = p.graph.nodes;
  for (std::size_t i = 0; i < nodes.size(); ++i)
    if (nodes[i].id == id) return i;
  return std::nullopt;
}

bool name_taken(const Patch& p, std::string_view name) {
  if (index_of(p, name)) return true;
  for (const auto& g : p.layout.groups)
    if (g.name == name) return true;
  for (const auto& f : p.layout.faces)
    if (f.label == name) return true;
  return false;
}

std::string free_name(const Patch& p, const std::string& base, bool bareFirst) {
  if (bareFirst && !name_taken(p, base)) return base;
  for (int k = bareFirst ? 2 : 1;; ++k) {
    std::string candidate = base + std::to_string(k);
    if (!name_taken(p, candidate)) return candidate;
  }
}

void pin_lost_its_feeds(Node& n, const std::string& pin) {
  n.wires.erase(pin);
  const TypeDeclarations* d = type_declarations(n.type);
  if (const auto* param = d ? d->param(pin) : nullptr) n.values[pin] = param->defaultValue;
}

std::vector<std::string> detach_node(Patch& p, std::size_t index) {
  const Node gone = p.graph.nodes[index];
  p.graph.nodes.erase(p.graph.nodes.begin() + std::ptrdiff_t(index));
  for (auto& n : p.graph.nodes) drop_feeds_from(n, gone.id);
  if (p.graph.output == gone.id) p.graph.output.clear();
  p.layout.positions.erase(gone.id);
  for (auto& g : p.layout.groups) std::erase(g.members, gone.id);
  for (auto& f : p.layout.faces)
    std::erase_if(f.fields, [&](const auto& field) { return field.second == gone.id; });
  return reads_of(gone);
}

void drop_unread_face_nodes(Patch& p, std::vector<std::string> wereRead) {
  while (!wereRead.empty()) {
    const std::string id = wereRead.back();
    wereRead.pop_back();
    if (!face_names(p, id) || is_read(p, id)) continue;
    const auto index = index_of(p, id);
    if (!index) continue;
    const std::vector<std::string> more = detach_node(p, *index);
    wereRead.insert(wereRead.end(), more.begin(), more.end());
  }
}

}  // namespace mforce::edit_detail
