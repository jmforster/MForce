#pragma once
#include <algorithm>
#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "mforce/patch/graph.h"

namespace mforce {

// The cycles a graph's reads close with no tap in them, each as the sentence
// "a cycle with no tap in it: a -> b -> a". A node reads the nodes its wires
// that are not taps name and the nodes its dynamic pins name; an id names its
// first node. A depth-first walk from each node in the graph's order names a
// cycle at every read that returns to a node still on the walk's path, from
// that node along the path and back to it.
inline std::vector<std::string> untapped_cycles(const Graph& graph) {
  const auto& nodes = graph.nodes;
  std::map<std::string, size_t, std::less<>> index;
  for (size_t i = 0; i < nodes.size(); ++i) index.try_emplace(nodes[i].id, i);
  std::vector<std::vector<size_t>> reads(nodes.size());
  const auto read = [&](size_t i, const std::string& id) {
    auto it = index.find(id);
    if (it != index.end() && std::find(reads[i].begin(), reads[i].end(), it->second) == reads[i].end())
      reads[i].push_back(it->second);
  };
  for (size_t i = 0; i < nodes.size(); ++i) {
    for (const auto& [pin, feeds] : nodes[i].wires)
      for (const auto& w : feeds)
        if (!w.tap) read(i, w.source);
    for (const auto& [setting, source] : nodes[i].dynamicPins) read(i, source);
  }
  std::vector<std::string> cycles;
  std::vector<int> state(nodes.size(), 0);               // 0 unwalked, 1 on the path, 2 done
  std::vector<size_t> path;
  std::function<void(size_t)> walk = [&](size_t i) {
    state[i] = 1;
    path.push_back(i);
    for (size_t j : reads[i]) {
      if (state[j] == 0) { walk(j); continue; }
      if (state[j] != 1) continue;
      std::string cycle;
      for (auto k = std::find(path.begin(), path.end(), j); k != path.end(); ++k)
        cycle += nodes[*k].id + " -> ";
      cycles.push_back("a cycle with no tap in it: " + cycle + nodes[j].id);
    }
    path.pop_back();
    state[i] = 2;
  };
  for (size_t i = 0; i < nodes.size(); ++i)
    if (state[i] == 0) walk(i);
  return cycles;
}

} // namespace mforce
