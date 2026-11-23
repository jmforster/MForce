// The style table's file form: JSON under styles/.
#include "mforce/compose/style_table.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mforce {

namespace {

using json = nlohmann::json;

StyleTable parse_json(const json& j) {
  StyleTable st;
  st.name = j.value("name", "");
  st.description = j.value("description", "");
  st.preferredChordBeats = j.value("preferredChordBeats", 0.0f);

  if (j.contains("transitions")) {
    for (auto& [label, arr] : j["transitions"].items()) {
      std::vector<StyleTable::Transition> ts;
      for (auto& entry : arr) {
        StyleTable::Transition t;
        t.target = ChordLabel::parse(entry[0].get<std::string>());
        t.weight = entry[1].get<float>();
        ts.push_back(t);
      }
      st.transitions[label] = std::move(ts);
    }
  }

  if (j.contains("overrides")) {
    for (auto& [key, arr] : j["overrides"].items()) {
      std::vector<StyleTable::Transition> ts;
      for (auto& entry : arr) {
        StyleTable::Transition t;
        t.target = ChordLabel::parse(entry[0].get<std::string>());
        t.weight = entry[1].get<float>();
        ts.push_back(t);
      }
      st.overrides[key] = std::move(ts);
    }
  }

  return st;
}

} // namespace

StyleTable StyleTable::load(const std::string& path) {
  std::ifstream f(path);
  if (!f.is_open()) throw std::runtime_error("Cannot open style table: " + path);
  json j = json::parse(f);
  return parse_json(j);
}

StyleTable StyleTable::load_by_name(const std::string& styleName) {
  std::string path = "styles/" + styleName + ".json";
  return load(path);
}

} // namespace mforce
