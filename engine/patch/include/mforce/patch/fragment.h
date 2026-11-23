#pragma once
#include <map>
#include <string>
#include <vector>
#include "mforce/patch/layout.h"
#include "mforce/patch/node.h"

namespace mforce {

// A piece of a graph on its way between patches. Its feeds may name nodes it
// does not hold, which is why it is not a Patch.
struct Fragment {
  std::vector<Node> nodes;
  std::map<std::string, Vec2> positions;                 // by node id
  std::vector<Group> groups;
  std::vector<NoteFace> faces;
  bool operator==(const Fragment&) const = default;
};

} // namespace mforce
