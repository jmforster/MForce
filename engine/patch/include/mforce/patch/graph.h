#pragma once
#include <string>
#include <vector>
#include "mforce/patch/node.h"

namespace mforce {

// A graph as data: its nodes in the file's order, and the id of the node
// whose signal is the graph's output (empty while the graph is assembled).
struct Graph {
  std::vector<Node> nodes;
  std::string output;
  bool operator==(const Graph&) const = default;
};

} // namespace mforce
