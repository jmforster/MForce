#pragma once
#include "mforce/music/basics.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <stdexcept>
#include <algorithm>
#include <fstream>
#include <cctype>

namespace mforce {

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// ChordLabel — parse Roman-numeral chord labels into ScaleChord.
//
// Format: [b|#]<Roman>[quality]
//   Roman: I ii III iv V vi vii (case encodes major/minor default)
//   Quality override: M, m, 7, M7, m7, dim, aug, o (= dim), + (= aug)
//   Alteration prefix: b = flat (-1), # = sharp (+1)
//
// Examples: "I" -> degree 0 Major, "V7" -> degree 4 Dom7,
//           "bVII" -> degree 6 alt -1 Major
// ---------------------------------------------------------------------------
struct ChordLabel {
  static ScaleChord parse(const std::string& label) {
    if (label.empty()) throw std::runtime_error("Empty chord label");

    int pos = 0;
    int alteration = 0;

    if (label[pos] == 'b') { alteration = -1; ++pos; }
    else if (label[pos] == '#') { alteration = 1; ++pos; }

    int degree = -1;
    bool defaultMinor = false;
    std::string roman;

    while (pos < (int)label.size()) {
      char c = label[pos];
      char cu = (char)std::toupper((unsigned char)c);
      if (cu != 'I' && cu != 'V') break;
      roman += c;
      ++pos;
    }

    if (roman.empty()) throw std::runtime_error("No Roman numeral in chord label: " + label);

    std::string upper = roman;
    for (auto& ch : upper) ch = (char)std::toupper((unsigned char)ch);

    if      (upper == "I")    degree = 0;
    else if (upper == "II")   degree = 1;
    else if (upper == "III")  degree = 2;
    else if (upper == "IV")   degree = 3;
    else if (upper == "V")    degree = 4;
    else if (upper == "VI")   degree = 5;
    else if (upper == "VII")  degree = 6;
    else throw std::runtime_error("Unknown Roman numeral: " + roman + " in " + label);

    defaultMinor = std::islower((unsigned char)roman[0]);

    std::string qualitySuffix = label.substr(pos);
    std::string qualityName;

    if (qualitySuffix.empty()) {
      qualityName = defaultMinor ? "Minor" : "Major";
    } else if (qualitySuffix == "M")   { qualityName = "Major"; }
    else if (qualitySuffix == "m")     { qualityName = "Minor"; }
    else if (qualitySuffix == "7")     { qualityName = "7"; }
    else if (qualitySuffix == "M7")    { qualityName = "Major7"; }
    else if (qualitySuffix == "m7")    { qualityName = "Minor7"; }
    else if (qualitySuffix == "dim" || qualitySuffix == "o")  { qualityName = "Diminished"; }
    else if (qualitySuffix == "aug" || qualitySuffix == "+")  { qualityName = "Augmented"; }
    else {
      qualityName = qualitySuffix;
    }

    ScaleChord sc;
    sc.degree = degree;
    sc.alteration = alteration;
    sc.quality = &ChordDef::get(qualityName);
    return sc;
  }

  static std::string to_string(const ScaleChord& sc) {
    static const char* romans[] = {"I", "II", "III", "IV", "V", "VI", "VII"};
    static const char* romansLower[] = {"i", "ii", "iii", "iv", "v", "vi", "vii"};

    std::string result;
    if (sc.alteration == -1) result += 'b';
    else if (sc.alteration == 1) result += '#';

    bool isMinor = sc.quality && (sc.quality->name == "Minor" || sc.quality->name == "Minor7");
    int deg = sc.degree % 7;
    result += isMinor ? romansLower[deg] : romans[deg];

    if (sc.quality) {
      const auto& qn = sc.quality->name;
      if (qn == "7") result += "7";
      else if (qn == "Major7") result += "M7";
      else if (qn == "Minor7") result += "m7";
      else if (qn == "Diminished") result += "o";
      else if (qn == "Augmented") result += "+";
    }
    return result;
  }
};

// ---------------------------------------------------------------------------
// StyleTable — chord transition graph with variable-order back-off.
// ---------------------------------------------------------------------------
struct StyleTable {
  struct Transition {
    ScaleChord target;
    float weight{1.0f};
  };

  std::string name;
  std::string description;

  std::unordered_map<std::string, std::vector<Transition>> transitions;
  std::unordered_map<std::string, std::vector<Transition>> overrides;

  float preferredChordBeats{0.0f};

  const std::vector<Transition>* lookup(const std::string& currentLabel,
                                         const std::vector<std::string>& history) const {
    if (!history.empty()) {
      std::string key = history.back() + "," + currentLabel;
      auto it = overrides.find(key);
      if (it != overrides.end()) return &it->second;
    }
    auto it = transitions.find(currentLabel);
    if (it != transitions.end()) return &it->second;
    return nullptr;
  }

  static StyleTable load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot open style table: " + path);
    json j = json::parse(f);
    return parse_json(j);
  }

  static StyleTable load_by_name(const std::string& styleName) {
    std::string path = "styles/" + styleName + ".json";
    return load(path);
  }

  static StyleTable parse_json(const json& j) {
    StyleTable st;
    st.name = j.value("name", "");
    st.description = j.value("description", "");
    st.preferredChordBeats = j.value("preferredChordBeats", 0.0f);

    if (j.contains("transitions")) {
      for (auto& [label, arr] : j["transitions"].items()) {
        std::vector<Transition> ts;
        for (auto& entry : arr) {
          Transition t;
          t.target = ChordLabel::parse(entry[0].get<std::string>());
          t.weight = entry[1].get<float>();
          ts.push_back(t);
        }
        st.transitions[label] = std::move(ts);
      }
    }

    if (j.contains("overrides")) {
      for (auto& [key, arr] : j["overrides"].items()) {
        std::vector<Transition> ts;
        for (auto& entry : arr) {
          Transition t;
          t.target = ChordLabel::parse(entry[0].get<std::string>());
          t.weight = entry[1].get<float>();
          ts.push_back(t);
        }
        st.overrides[key] = std::move(ts);
      }
    }

    return st;
  }
};

} // namespace mforce
