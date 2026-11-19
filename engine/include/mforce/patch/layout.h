#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mforce {

// A point on the editor's canvas.
struct Vec2 {
  float x{0}, y{0};
  bool operator==(const Vec2&) const = default;
};

// A group of the editor's canvas: its name, its members (node ids, group
// names or face labels) and its position.
struct Group {
  std::string name;
  std::vector<std::string> members;
  Vec2 pos;
  bool operator==(const Group&) const = default;
};

// A note face: the one node the editor shows for the PerformNodes it names.
struct NoteFace {
  std::string label;
  std::map<std::string, std::string> fields;             // field -> the file node it reads
  std::optional<Vec2> pos;                               // the face's own position: a face is not a node, so `positions` does not hold it
  bool operator==(const NoteFace&) const = default;
};

// The editor's data the engine never reads: where each node sits, the Output
// node's position, the canvas panning, the groups and the note faces.
struct Layout {
  std::map<std::string, Vec2> positions;                 // by node id
  std::optional<Vec2> outputPos;                         // the editor's Output node
  std::optional<Vec2> panning;
  std::vector<Group> groups;
  std::vector<NoteFace> faces;
  bool operator==(const Layout&) const = default;
};

} // namespace mforce
