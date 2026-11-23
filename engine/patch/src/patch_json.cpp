#include "mforce/patch/patch_json.h"
#include "mforce/patch/retired_spellings.h"
#include "codec_path.h"
#include "float_text.h"
#include "sentence.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace mforce {

namespace {

using nlohmann::json;

// The wrong shape at `path`, whose last step is `key`: "'<key>' takes <what>, not <json>".
[[noreturn]] void wrong_shape(const std::string& path, const std::string& key, const char* what, const json& v) {
  refuse(path, in_quotes(key) + " takes " + what + ", not " + v.dump());
}

[[noreturn]] void unknown_key(const std::string& key) { refuse(key, "unknown key '" + key + "'"); }

std::string text_of(const std::string& key, const json& v) {
  if (!v.is_string()) wrong_shape(key, key, "a string", v);
  return v.get<std::string>();
}

float number_of(const std::string& key, const json& v) {
  if (!v.is_number()) wrong_shape(key, key, "a number", v);
  return float(v.get<double>());
}

double double_of(const std::string& key, const json& v) {
  if (!v.is_number()) wrong_shape(key, key, "a number", v);
  return v.get<double>();
}

// A whole number an int holds.
int whole_of(const std::string& key, const json& v) {
  const bool number = v.is_number();
  const double d = number ? v.get<double>() : 0.0;
  if (!number || !std::isfinite(d) || d != std::floor(d) || std::fabs(d) > double(std::numeric_limits<int>::max()))
    wrong_shape(key, key, "a whole number", v);
  return int(d);
}

bool flag_of(const std::string& key, const json& v) {
  if (!v.is_boolean()) wrong_shape(key, key, "true or false", v);
  return v.get<bool>();
}

std::vector<std::string> texts_of(const std::string& key, const json& v) {
  bool texts = v.is_array();
  for (size_t i = 0; texts && i < v.size(); ++i) texts = v[i].is_string();
  if (!texts) wrong_shape(key, key, "a list of strings", v);
  return v.get<std::vector<std::string>>();
}

// A point on the canvas: [x, y].
Vec2 point_of(const std::string& path, const std::string& key, const json& v) {
  if (!v.is_array() || v.size() != 2 || !v[0].is_number() || !v[1].is_number()) wrong_shape(path, key, "[x, y]", v);
  return Vec2{float(v[0].get<double>()), float(v[1].get<double>())};
}

const json& list_of(const std::string& key, const json& v) {
  if (!v.is_array()) wrong_shape(key, key, "a list", v);
  return v;
}

const json& object_of(const std::string& path, const std::string& key, const json& v) {
  if (!v.is_object()) wrong_shape(path, key, "an object", v);
  return v;
}

// Each item of the list `v` under `key`, an object, read by `read` with its
// index as the path's step.
template <class Read>
void each_object(const std::string& key, const json& v, Read&& read) {
  const json& list = list_of(key, v);
  for (size_t i = 0; i < list.size(); ++i) {
    const std::string step = key + "[" + std::to_string(i) + "]";
    const json& item = object_of(step, step, list[i]);
    at_step(step, [&] { read(item); });
  }
}

std::vector<Node> read_nodes(const json& v) {
  std::vector<Node> nodes;
  each_object("nodes", v, [&](const json& item) {
    Node n;
    from_json(item, n);
    nodes.push_back(std::move(n));
  });
  return nodes;
}

std::map<std::string, Vec2> read_positions(const json& v) {
  std::map<std::string, Vec2> positions;
  const json& by_id = object_of("positions", "positions", v);
  for (auto it = by_id.begin(); it != by_id.end(); ++it)
    positions[it.key()] = point_of("positions." + it.key(), it.key(), it.value());
  return positions;
}

// A record read from the object `j`: each key by `key`, which reads it into
// the record and answers whether the record has it; a key it does not have
// is an unknown key.
template <class T>
T read_object(const json& j, bool (*key)(T&, const std::string&, const json&)) {
  T record;
  for (auto it = j.begin(); it != j.end(); ++it)
    if (!key(record, it.key(), it.value())) unknown_key(it.key());
  return record;
}

bool group_key(Group& g, const std::string& k, const json& v) {
  if (k == "name") g.name = text_of(k, v);
  else if (k == "members") g.members = texts_of(k, v);
  else if (k == "pos") g.pos = point_of(k, k, v);
  else return false;
  return true;
}

bool face_key(NoteFace& f, const std::string& k, const json& v) {
  if (k == "label") f.label = text_of(k, v);
  else if (k == "pos") f.pos = point_of(k, k, v);
  else if (k == "fields") {
    const json& fields = object_of(k, k, v);
    for (auto field = fields.begin(); field != fields.end(); ++field)
      at_step(k, [&] { f.fields[field.key()] = text_of(field.key(), field.value()); });
  } else return false;
  return true;
}

std::vector<Group> read_groups(const json& v) {
  std::vector<Group> groups;
  each_object("groups", v, [&](const json& item) { groups.push_back(read_object(item, group_key)); });
  return groups;
}

std::vector<NoteFace> read_faces(const json& v) {
  std::vector<NoteFace> faces;
  each_object("faces", v, [&](const json& item) { faces.push_back(read_object(item, face_key)); });
  return faces;
}

bool graph_key(Graph& g, const std::string& k, const json& v) {
  if (k == "output") g.output = text_of(k, v);
  else if (k == "nodes") g.nodes = read_nodes(v);
  else return false;
  return true;
}

bool block_key(InstrumentBlock& b, const std::string& k, const json& v) {
  if (k == "polyphony") b.polyphony = whole_of(k, v);
  else if (k == "volume") b.volume = number_of(k, v);
  else if (k == "sustaining") b.sustaining = flag_of(k, v);
  else if (k == "onsets") b.onsets = texts_of(k, v);
  else if (k == "glideMs") b.glideMs = number_of(k, v);
  else if (k == "paramMap") refuse(k, kParamMapRefusal);
  else return false;
  return true;
}

bool layout_key(Layout& l, const std::string& k, const json& v) {
  if (k == "positions") l.positions = read_positions(v);
  else if (k == "output") l.outputPos = point_of(k, k, v);
  else if (k == "panning") l.panning = point_of(k, k, v);
  else if (k == "groups") l.groups = read_groups(v);
  else if (k == "faces") l.faces = read_faces(v);
  else return false;
  return true;
}

bool fragment_key(Fragment& f, const std::string& k, const json& v) {
  if (k == "nodes") f.nodes = read_nodes(v);
  else if (k == "positions") f.positions = read_positions(v);
  else if (k == "groups") f.groups = read_groups(v);
  else if (k == "faces") f.faces = read_faces(v);
  else return false;
  return true;
}

Graph read_graph(const json& j) { return read_object(j, graph_key); }
InstrumentBlock read_block(const json& j) { return read_object(j, block_key); }
Layout read_layout(const json& j) { return read_object(j, layout_key); }
Fragment read_fragment(const json& j) { return read_object(j, fragment_key); }

// One key at the top of the document into `p`; false for a key the record
// does not have.
bool read_top_key(Patch& p, const std::string& k, const json& v) {
  if (k == "sampleRate") p.sampleRate = whole_of(k, v);
  else if (k == "seconds") p.seconds = double_of(k, v);
  else if (k == "comment") p.comment = text_of(k, v);
  else if (k == "score") p.score = v.dump();
  else if (k == "instrument") at_step(k, [&, &o = object_of(k, k, v)] { p.instrument = read_block(o); });
  else if (k == "graph") at_step(k, [&, &o = object_of(k, k, v)] { p.graph = read_graph(o); });
  else if (k == "layout") at_step(k, [&, &o = object_of(k, k, v)] { p.layout = read_layout(o); });
  else return false;
  return true;
}

Patch read_patch(const json& j) {
  Patch p;
  for (auto it = j.begin(); it != j.end(); ++it) {
    const std::string& k = it.key();
    if (read_top_key(p, k, it.value())) continue;
    const std::string retired = retired_key_refusal("", k, RetiredAt::TopKey);
    if (!retired.empty()) refuse(k, retired);
    unknown_key(k);
  }
  const std::vector<std::string> violations = p.check();
  if (!violations.empty()) {
    std::string lines;
    for (const auto& v : violations) lines += (lines.empty() ? "" : "\n") + v;
    throw std::runtime_error(lines);
  }
  return p;
}

json number_json(float v) { return json(float_number(v)); }

json point_json(const Vec2& v) { return json::array({number_json(v.x), number_json(v.y)}); }

json positions_json(const std::map<std::string, Vec2>& positions) {
  json out = json::object();
  for (const auto& [id, pos] : positions) out[id] = point_json(pos);
  return out;
}

json groups_json(const std::vector<Group>& groups) {
  json out = json::array();
  for (const auto& g : groups) out.push_back(json{{"name", g.name}, {"members", g.members}, {"pos", point_json(g.pos)}});
  return out;
}

json faces_json(const std::vector<NoteFace>& faces) {
  json out = json::array();
  for (const auto& f : faces) {
    json face = {{"label", f.label}, {"fields", f.fields}};
    if (f.pos) face["pos"] = point_json(*f.pos);
    out.push_back(std::move(face));
  }
  return out;
}

bool has_layout(const Layout& l) {
  return !l.positions.empty() || l.outputPos || l.panning || !l.groups.empty() || !l.faces.empty();
}

// The read record of `j` by `read`, assigned to `target` only once it is whole.
template <class T, class Read>
void read_whole(const json& j, T& target, const char* what, Read&& read) {
  if (!j.is_object()) throw std::runtime_error(std::string(what) + " takes an object, not " + j.dump());
  T result = read(j);
  target = std::move(result);
}

} // namespace

void from_json(const nlohmann::json& j, Graph& graph) { read_whole(j, graph, "a graph", read_graph); }
void from_json(const nlohmann::json& j, InstrumentBlock& block) { read_whole(j, block, "an instrument block", read_block); }
void from_json(const nlohmann::json& j, Layout& layout) { read_whole(j, layout, "a layout", read_layout); }
void from_json(const nlohmann::json& j, Fragment& fragment) { read_whole(j, fragment, "a fragment", read_fragment); }
void from_json(const nlohmann::json& j, Patch& patch) { read_whole(j, patch, "a patch", read_patch); }

void to_json(nlohmann::json& j, const Graph& graph) {
  json nodes = json::array();
  for (const auto& n : graph.nodes) nodes.push_back(json(n));
  j = json{{"output", graph.output}, {"nodes", std::move(nodes)}};
}

void to_json(nlohmann::json& j, const InstrumentBlock& block) {
  j = json{{"polyphony", block.polyphony}, {"volume", number_json(block.volume)}, {"sustaining", block.sustaining},
           {"onsets", block.onsets}, {"glideMs", number_json(block.glideMs)}};
}

void to_json(nlohmann::json& j, const Layout& layout) {
  json out = json::object();
  if (!layout.positions.empty()) out["positions"] = positions_json(layout.positions);
  if (layout.outputPos) out["output"] = point_json(*layout.outputPos);
  if (layout.panning) out["panning"] = point_json(*layout.panning);
  if (!layout.groups.empty()) out["groups"] = groups_json(layout.groups);
  if (!layout.faces.empty()) out["faces"] = faces_json(layout.faces);
  j = std::move(out);
}

void to_json(nlohmann::json& j, const Fragment& fragment) {
  json nodes = json::array();
  for (const auto& n : fragment.nodes) nodes.push_back(json(n));
  json out = {{"nodes", std::move(nodes)}};
  if (!fragment.positions.empty()) out["positions"] = positions_json(fragment.positions);
  if (!fragment.groups.empty()) out["groups"] = groups_json(fragment.groups);
  if (!fragment.faces.empty()) out["faces"] = faces_json(fragment.faces);
  j = std::move(out);
}

void to_json(nlohmann::json& j, const Patch& patch) {
  json out = json::object();
  out["sampleRate"] = patch.sampleRate;
  if (patch.seconds) out["seconds"] = *patch.seconds;
  if (!patch.comment.empty()) out["comment"] = patch.comment;
  if (patch.instrument) out["instrument"] = json(*patch.instrument);
  if (!patch.score.empty()) {
    json score = json::parse(patch.score, nullptr, false);
    out["score"] = score.is_discarded() ? json(patch.score) : std::move(score);   // text that is not JSON rides as a string
  }
  out["graph"] = json(patch.graph);
  if (has_layout(patch.layout)) out["layout"] = json(patch.layout);
  j = std::move(out);
}

Patch load_patch(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error(path + ": cannot open the file");
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  Patch p;
  try {
    from_json(json::parse(text), p);
  } catch (const std::exception& e) {   // a parse error, or a refusal with its path
    throw std::runtime_error(path + ": " + e.what());
  }
  return p;
}

void save_patch(const std::string& path, const Patch& patch) {
  const std::string text = json(patch).dump(2, ' ', false, json::error_handler_t::replace) + "\n";
  const std::string temporary = path + ".tmp";
  {
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    out.write(text.data(), std::streamsize(text.size()));
    out.close();
    if (!out) {
      std::error_code removed;
      std::filesystem::remove(temporary, removed);
      throw std::runtime_error(path + ": cannot write " + temporary);
    }
  }
  std::error_code ec;
  std::filesystem::rename(temporary, path, ec);
  if (ec) {
    std::filesystem::remove(temporary, ec);
    throw std::runtime_error(path + ": cannot replace the file with " + temporary);
  }
}

} // namespace mforce
