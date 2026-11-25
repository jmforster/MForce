// A piece of a patch cut out (fragment_of) and put into a patch (paste).
#include "mforce/patch/edit.h"
#include <cctype>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "edit_support.h"
#include "sentence.h"

namespace mforce {

using namespace edit_detail;   // the shared helpers by their short names (C#: a `using` directive)

namespace {

// The note face that names node `id` for a field; null when none does.
const NoteFace* face_naming(const Patch& p, std::string_view id) {
  for (const auto& f : p.layout.faces)
    for (const auto& [field, node] : f.fields)
      if (node == id) return &f;
  return nullptr;
}

// `id` less its trailing digits; the whole id when that leaves nothing.
std::string stem_of(const std::string& id) {
  std::size_t end = id.size();
  while (end > 0 && std::isdigit(static_cast<unsigned char>(id[end - 1]))) --end;
  return end == 0 ? id : id.substr(0, end);
}

// The name a pasted node takes: its own when nothing has it, else its stem
// with the first number from 2 that nothing has. `given` holds the names
// this paste has already handed out.
std::string pasted_name(const Patch& p, const std::string& id, const std::set<std::string>& given) {
  const auto free = [&](const std::string& name) { return !name_taken(p, name) && !given.count(name); };
  if (free(id)) return id;
  const std::string stem = stem_of(id);
  for (int k = 2;; ++k)
    if (free(stem + std::to_string(k))) return stem + std::to_string(k);
}

// Where a feed of a pasted node now reads: the new id of a fragment node,
// a node of the patch kept as it is, or nothing (empty).
std::string read_after_paste(const std::map<std::string, std::string>& renamed, const Patch& p,
                             const std::string& source) {
  if (const auto it = renamed.find(source); it != renamed.end()) return it->second;
  return index_of(p, source) ? source : std::string();
}

// Every feed and drive of `n` led to where it reads after the paste; each
// that reads nothing goes and is counted in `dropped`.
void lead_feeds(Node& n, const std::map<std::string, std::string>& renamed, const Patch& p, int& dropped) {
  std::vector<std::string> emptied;
  for (auto& [pin, feeds] : n.wires) {
    std::vector<Wire> kept;
    for (const auto& w : feeds) {
      const std::string to = read_after_paste(renamed, p, w.source);
      if (to.empty()) ++dropped;
      else kept.push_back(Wire{to, w.tap});
    }
    feeds = std::move(kept);
    if (feeds.empty()) emptied.push_back(pin);
  }
  for (const auto& pin : emptied) pin_lost_its_feeds(n, pin);
  std::map<std::string, std::string> drives;
  for (const auto& [setting, source] : n.dynamicPins) {
    const std::string to = read_after_paste(renamed, p, source);
    if (to.empty()) ++dropped;
    else drives[setting] = to;
  }
  n.dynamicPins = std::move(drives);
}

}  // namespace

Fragment fragment_of(const Patch& p, const std::vector<std::string>& nodes) {
  Fragment f;
  std::set<std::string> taken;
  for (const auto& id : nodes) {
    if (!taken.insert(id).second) continue;
    const auto index = index_of(p, id);
    if (!index) throw std::runtime_error(in_quotes(id) + " names no node: it cannot be copied");
    if (const NoteFace* face = face_naming(p, id))
      throw std::runtime_error(in_quotes(id) + " is a node of note face " + in_quotes(face->label) +
                               ": a note face's node is not copied");
    f.nodes.push_back(p.graph.nodes[*index]);
    if (const auto pos = p.layout.positions.find(id); pos != p.layout.positions.end()) f.positions[id] = pos->second;
  }
  return f;
}

Pasted paste(Patch& p, const Fragment& f, Vec2 offset) {
  if (f.nodes.empty()) throw std::runtime_error("the fragment holds no nodes: there is nothing to paste");
  if (!f.groups.empty() || !f.faces.empty())
    throw std::runtime_error("the fragment holds groups or note faces, which are not pasted");
  require_finite(offset, "the paste's offset");
  Pasted done;
  std::map<std::string, std::string> renamed;
  std::set<std::string> given;
  for (const auto& n : f.nodes) {
    const std::string id = pasted_name(p, n.id, given);
    given.insert(id);
    renamed.try_emplace(n.id, id);
    done.ids.push_back(id);
  }
  Patch copy = p;
  for (std::size_t k = 0; k < f.nodes.size(); ++k) {
    Node n = f.nodes[k];
    n.id = done.ids[k];
    lead_feeds(n, renamed, p, done.dropped);
    if (const auto pos = f.positions.find(f.nodes[k].id); pos != f.positions.end())
      copy.layout.positions[n.id] = Vec2{pos->second.x + offset.x, pos->second.y + offset.y};
    copy.graph.nodes.push_back(std::move(n));
  }
  (void)adopt(p, copy, Change::Structure);
  return done;
}

} // namespace mforce
