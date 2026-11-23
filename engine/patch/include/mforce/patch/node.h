#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mforce {

// One feed of a pin: the node it reads, and whether through a tap, which reads
// the value that node held at the end of the previous sample and so may close
// a loop.
struct Wire {
  std::string source;
  bool tap{false};
  bool operator==(const Wire&) const = default;
};

// One node of a graph as data: its id, its type, and what it holds for each
// name its type declares. A name sits in the map of its declaration's kind and
// in no other.
struct Node {
  std::string id;
  std::string type;
  std::optional<uint32_t> seed;                          // random at creation, stored for reproducibility
  std::map<std::string, float>              values;      // a pin's constant, while nothing feeds it
  std::map<std::string, std::vector<Wire>>  wires;       // a pin's feeds, in order: one, or several on a list or a multi slot; never empty
  std::map<std::string, float>              settings;    // a scalar setting as the engine stores it: a bool is 0 or 1, an enum its index
  std::map<std::string, std::string>        strings;     // a string setting
  std::map<std::string, std::vector<float>> arrays;      // an array, or one column of a table; a labelled column holds indices
  std::map<std::string, std::string>        dynamicPins; // a setting and the node that feeds it once per note
  std::string comment;
  bool operator==(const Node&) const = default;
};

} // namespace mforce
