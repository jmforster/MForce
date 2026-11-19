#include "mforce/patch/patch_json.h"
#include "mforce/core/source_registry.h"
#include "mforce/patch/envelope_preset.h"
#include "mforce/patch/retired_spellings.h"
#include "mforce/patch/type_declarations.h"
#include "codec_path.h"
#include "float_text.h"
#include "sentence.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mforce {

namespace {

using nlohmann::json;
using Decl = TypeDeclarations;

bool is_whole(const json& v) {
  if (!v.is_number()) return false;
  const double d = v.get<double>();
  return std::isfinite(d) && d == std::floor(d);
}

float number_of(const json& v) { return float(v.get<double>()); }

// The node being read: its record so far, its declarations and the words a
// refusal about it opens with. Every path is relative to the node.
struct Reading {
  Node node;
  const Decl& d;
  std::string opening;
  [[noreturn]] void fail(const std::string& path, const std::string& tail) const { refuse(path, opening + tail); }
};

std::string param_path(const std::string& key) { return "params." + key; }

// A feed object: 'ref' or 'tap' alone, holding an id. Empty when `v` is not
// a feed: not an object, neither key, or an id that is not a string. Both
// keys, or a key beside them, are refused with the loader's sentences.
std::optional<Wire> feed_of(const Reading& r, const std::string& path, const std::string& pin, const json& v) {
  if (!v.is_object()) return std::nullopt;
  const bool ref = v.contains("ref"), tap = v.contains("tap");
  if (ref && tap) r.fail(path, in_quotes(pin) + ": a feed holds 'ref' or 'tap', not both");
  if (!ref && !tap) return std::nullopt;
  for (auto it = v.begin(); it != v.end(); ++it) {
    if (it.key() == "ref" || it.key() == "tap") continue;
    const std::string retired = retired_key_refusal(r.node.type, it.key(), RetiredAt::FeedKey);
    r.fail(path, in_quotes(pin) + ": " +
                 (retired.empty() ? "a feed holds 'ref' or 'tap' alone, not '" + it.key() + "'" : retired));
  }
  const json& id = v.at(ref ? "ref" : "tap");
  if (!id.is_string()) return std::nullopt;
  return Wire{id.get<std::string>(), tap};
}

// The loader's sentence for a string where a pin or a slot takes a feed.
std::string string_on_pin(const std::string& pin, const json& v, bool slot) {
  std::string s = in_quotes(pin) + " is the string \"" + v.get<std::string>() + "\"; " +
                  (slot ? "a slot takes {\"ref\": id}" : "a pin takes a number, {\"ref\": id} or {\"tap\": id}");
  if (pin == "evolution")
    s += "; the string-typed evolution is gone, python tools/convert_patch.py <patch> rewrites it as an evolution node";
  return s;
}

// A Param, or an Input that takes signals: a number, one feed, or on a list
// pin a list of feeds. An empty list stores nothing.
void read_signal(Reading& r, const std::string& pin, const json& v, bool list) {
  const std::string path = param_path(pin);
  if (v.is_number()) { r.node.values[pin] = number_of(v); return; }
  if (v.is_string()) r.fail(path, string_on_pin(pin, v, false));
  if (v.is_array()) {
    if (!list) r.fail(path, in_quotes(pin) + " takes one feed, not a list");
    std::vector<Wire> feeds;
    for (size_t k = 0; k < v.size(); ++k) {
      const std::string at = path + "[" + std::to_string(k) + "]";
      const auto w = feed_of(r, at, pin, v[k]);
      if (!w) r.fail(at, in_quotes(pin) + " holds " + v[k].dump() + ", where a list holds {\"ref\": id} or {\"tap\": id}");
      feeds.push_back(*w);
    }
    if (!feeds.empty()) r.node.wires[pin] = std::move(feeds);
    return;
  }
  const auto w = feed_of(r, path, pin, v);
  if (!w) r.fail(path, in_quotes(pin) + " takes a number, {\"ref\": id} or {\"tap\": id}, not " + v.dump());
  r.node.wires[pin] = {*w};
}

// One feed of a slot: {"ref": id} and nothing else.
Wire slot_feed(const Reading& r, const std::string& path, const std::string& pin, const json& v) {
  if (v.is_object() && !v.contains("ref") && !v.contains("tap")) {
    const std::string retired = retired_key_refusal(r.node.type, pin, RetiredAt::InlineSlot);
    r.fail(path, retired.empty() ? in_quotes(pin) + " holds an object where a slot takes {\"ref\": id}" : retired);
  }
  const auto w = feed_of(r, path, pin, v);
  if (!w || w->tap) r.fail(path, in_quotes(pin) + " is a slot: it takes {\"ref\": id}, not " + v.dump());
  return *w;
}

// A slot: one feed, or on a multi slot a list of them. An empty list stores nothing.
void read_slot(Reading& r, const std::string& pin, const json& v, bool multi) {
  const std::string path = param_path(pin);
  if (v.is_string()) r.fail(path, string_on_pin(pin, v, true));
  if (!v.is_array()) { r.node.wires[pin] = {slot_feed(r, path, pin, v)}; return; }
  if (!multi) r.fail(path, in_quotes(pin) + " takes one feed, not a list");
  std::vector<Wire> feeds;
  for (size_t k = 0; k < v.size(); ++k)
    feeds.push_back(slot_feed(r, path + "[" + std::to_string(k) + "]", pin, v[k]));
  if (!feeds.empty()) r.node.wires[pin] = std::move(feeds);
}

// A label equal to one of `labels` exactly, or a whole number inside them, as its index.
float label_index(const Reading& r, const std::string& name, const std::vector<std::string>& labels, const json& v) {
  if (v.is_string()) {
    const auto it = std::find(labels.begin(), labels.end(), v.get<std::string>());
    if (it != labels.end()) return float(it - labels.begin());
  } else if (is_whole(v) && v.get<double>() >= 0.0 && v.get<double>() < double(labels.size())) {
    return number_of(v);
  }
  r.fail(param_path(name), in_quotes(name) + " holds " + v.dump() + ", which is not one of its labels (" + comma_list(labels) +
                           ") or an index into them");
}

void read_setting(Reading& r, const Decl::Setting& s, const json& v) {
  const std::string& k = s.name;
  if (s.type == SettingType::String) {
    if (!v.is_string()) r.fail(param_path(k), in_quotes(k) + " takes a string, not " + v.dump());
    r.node.strings[k] = v.get<std::string>();
    return;
  }
  if (s.type == SettingType::Bool) {
    if (v.is_boolean()) { r.node.settings[k] = v.get<bool>() ? 1.0f : 0.0f; return; }
    if (v.is_number() && (v.get<double>() == 0.0 || v.get<double>() == 1.0)) { r.node.settings[k] = number_of(v); return; }
    r.fail(param_path(k), in_quotes(k) + " takes true or false, not " + v.dump());
  }
  if (!s.labels.empty()) { r.node.settings[k] = label_index(r, k, s.labels, v); return; }
  if (!v.is_number()) r.fail(param_path(k), in_quotes(k) + " takes a number, not " + v.dump());
  if (s.type == SettingType::Int && !is_whole(v)) r.fail(param_path(k), in_quotes(k) + " takes a whole number, not " + v.dump());
  r.node.settings[k] = number_of(v);
}

// An array: a list of numbers; a labelled column's rows labels or indices.
void read_array(Reading& r, const Decl::Array& a, const json& v) {
  const std::string& k = a.name;
  if (!v.is_array()) r.fail(param_path(k), in_quotes(k) + " takes a list of numbers, not " + v.dump());
  std::vector<float> rows;
  rows.reserve(v.size());
  for (const auto& item : v) {
    if (!a.labels.empty()) { rows.push_back(label_index(r, k, a.labels, item)); continue; }
    if (!item.is_number()) r.fail(param_path(k), in_quotes(k) + " takes a list of numbers, not " + v.dump());
    rows.push_back(number_of(item));
  }
  r.node.arrays[k] = std::move(rows);
}

void read_seed(Reading& r, const json& v) {
  if (!is_whole(v) || v.get<double>() < 0.0 || v.get<double>() > 4294967295.0)
    r.fail(param_path("seed"), "'seed' takes a whole number from 0 to 4294967295, not " + v.dump());
  r.node.seed = uint32_t(v.get<double>());
}

// One key of the bag by the declaration it names; false when the type declares no such name.
bool read_declared(Reading& r, const std::string& key, const json& v) {
  if (key == "seed") { read_seed(r, v); return true; }
  if (r.d.param(key)) { read_signal(r, key, v, false); return true; }
  if (const auto* in = r.d.input(key)) {
    if (in->slot) read_slot(r, key, v, in->list);
    else read_signal(r, key, v, in->list);
    return true;
  }
  if (const auto* s = r.d.setting(key)) { read_setting(r, *s, v); return true; }
  if (const auto* a = r.d.array(key)) { read_array(r, *a, v); return true; }
  return false;
}

// Every key of the bag: a retired spelling by its sentence, then a declared
// name by its kind, then one of `consumed`, else an unknown key.
void read_keys(Reading& r, const json& params, const std::vector<std::string>* consumed) {
  for (auto it = params.begin(); it != params.end(); ++it) {
    const std::string& key = it.key();
    const std::string retired = retired_key_refusal(r.node.type, key);
    if (!retired.empty()) r.fail(param_path(key), retired);
    if (read_declared(r, key, it.value())) continue;
    if (consumed && std::find(consumed->begin(), consumed->end(), key) != consumed->end()) continue;
    r.fail(param_path(key), "unknown key '" + key + "'");
  }
}

// An Envelope holds its stage columns or the preset form, never both. The
// preset form's columns are those envelope_from_preset_json lays out; its
// declared keys are then read as any node's, and its other knob keys are
// consumed.
void read_envelope(Reading& r, const json& params) {
  std::vector<std::string> columns;
  for (const auto& a : r.d.arrays)
    if (a.group == "stages") columns.push_back(a.name);
  for (const auto& c : columns) {
    if (!params.contains(c)) continue;
    if (params.contains("preset"))
      r.fail(param_path("preset"), "holds both 'preset' and '" + c + "'; an Envelope takes one form: the stage columns or a preset");
    read_keys(r, params, nullptr);
    return;
  }
  std::shared_ptr<Envelope> preset;
  try {
    preset = envelope_from_preset_json(params);
  } catch (const std::exception& e) {
    r.fail("params", e.what());
  }
  for (const auto& c : columns) r.node.arrays[c] = preset->get_array(c);
  read_keys(r, params, &envelope_preset_keys());
}

// The model is complete in pins and settings: a Param with neither a number
// nor a feed takes its declared default, a setting the file left out its
// default, a string setting its default text. Inputs and arrays take nothing.
void complete(Node& n, const Decl& d) {
  for (const auto& p : d.params)
    if (!n.values.count(p.name) && !n.wires.count(p.name)) n.values[p.name] = p.defaultValue;
  for (const auto& s : d.settings) {
    if (s.type == SettingType::String) {
      if (!n.strings.count(s.name)) n.strings[s.name] = s.defaultText;
    } else if (!n.settings.count(s.name)) {
      n.settings[s.name] = s.defaultValue;
    }
  }
}

std::string text_key(const json& j, const char* key) {
  if (!j.contains(key)) return {};
  const json& v = j.at(key);
  if (!v.is_string()) refuse(key, in_quotes(key) + " takes a string, not " + v.dump());
  return v.get<std::string>();
}

// The node's own keys beside params: id, type, params, dynamicPins, comment.
void read_node_keys(const json& j, const std::string& type, const std::string& opening) {
  for (auto it = j.begin(); it != j.end(); ++it) {
    const std::string& key = it.key();
    if (key == "id" || key == "type" || key == "params" || key == "dynamicPins" || key == "comment") continue;
    const std::string retired = retired_key_refusal(type, key, RetiredAt::NodeKey);
    refuse(key, opening + (retired.empty() ? "unknown key '" + key + "'" : retired));
  }
}

// dynamicPins: an object whose every value is {"ref": id} and nothing else.
void read_dynamic_pins(Reading& r, const json& v) {
  if (!v.is_object()) r.fail("dynamicPins", "'dynamicPins' takes an object, not " + v.dump());
  for (auto it = v.begin(); it != v.end(); ++it) {
    const json& feed = it.value();
    if (!feed.is_object() || feed.size() != 1 || !feed.contains("ref") || !feed.at("ref").is_string())
      r.fail("dynamicPins." + it.key(), in_quotes(it.key()) + " in dynamicPins takes {\"ref\": id}, not " + feed.dump());
    r.node.dynamicPins[it.key()] = feed.at("ref").get<std::string>();
  }
}

Node read_node(const json& j) {
  Node n;
  n.id = text_key(j, "id");
  n.type = text_key(j, "type");
  const std::string opening = "node " + in_quotes(n.id) + " (" + n.type + "): ";
  read_node_keys(j, n.type, opening);
  const Decl* d = type_declarations(n.type);
  if (!d) {
    std::string sentence = "Unknown node type: " + n.type;
    if (const char* hint = retired_type_hint(n.type)) sentence += std::string("; ") + hint;
    refuse("type", opening + sentence);
  }
  Reading r{std::move(n), *d, opening};
  if (j.contains("comment")) {
    if (!j.at("comment").is_string()) r.fail("comment", "'comment' takes a string, not " + j.at("comment").dump());
    r.node.comment = j.at("comment").get<std::string>();
  }
  if (j.contains("dynamicPins")) read_dynamic_pins(r, j.at("dynamicPins"));
  static const json kEmpty = json::object();
  const json& params = j.contains("params") ? j.at("params") : kEmpty;
  if (!params.is_object()) r.fail("params", "'params' takes an object, not " + params.dump());
  if (r.node.type == "Envelope") read_envelope(r, params);
  else read_keys(r, params, nullptr);
  complete(r.node, *d);
  return std::move(r.node);
}

json number_json(float v) { return json(float_number(v)); }

json feed_json(const Wire& w) { return json{{w.tap ? "tap" : "ref", w.source}}; }

// A pin's feeds: one object on a pin that takes one, a list everywhere else.
json feeds_json(const std::vector<Wire>& feeds, bool single) {
  if (single && feeds.size() == 1) return feed_json(feeds[0]);
  json list = json::array();
  for (const auto& w : feeds) list.push_back(feed_json(w));
  return list;
}

// A scalar setting by its declaration: a bool as true or false, an enum as
// its label, an int as an integer, anything else (an undeclared name, a
// value its declaration does not take) as a number.
json setting_json(const Decl::Setting* s, float v) {
  if (s && s->type == SettingType::Bool && (v == 0.0f || v == 1.0f)) return json(v != 0.0f);
  if (s && !s->labels.empty() && is_index(v, s->labels.size())) return json(s->labels[size_t(v)]);
  if (s && s->type == SettingType::Int && s->labels.empty() && std::fabs(v) < 9.0e15f && v == std::trunc(v))
    return json(int64_t(v));
  return number_json(v);
}

json array_json(const Decl::Array* a, const std::vector<float>& rows) {
  json list = json::array();
  for (float v : rows) {
    if (a && !a->labels.empty() && is_index(v, a->labels.size())) list.push_back(a->labels[size_t(v)]);
    else list.push_back(number_json(v));
  }
  return list;
}

// The bag by kind from the type's declarations; a name the type does not
// declare, or any name of a type the registry lacks, by the map it sits in.
json params_json(const Node& n) {
  const Decl* d = type_declarations(n.type);
  json params = json::object();
  if (n.seed) params["seed"] = *n.seed;
  for (const auto& [k, v] : n.values) params[k] = number_json(v);
  for (const auto& [k, feeds] : n.wires) {
    const auto* in = d ? d->input(k) : nullptr;
    params[k] = feeds_json(feeds, d && (d->param(k) || (in && !in->list)));
  }
  for (const auto& [k, v] : n.settings) params[k] = setting_json(d ? d->setting(k) : nullptr, v);
  for (const auto& [k, v] : n.strings) params[k] = v;
  for (const auto& [k, rows] : n.arrays) params[k] = array_json(d ? d->array(k) : nullptr, rows);
  return params;
}

} // namespace

void from_json(const nlohmann::json& j, Node& node) {
  if (!j.is_object()) throw std::runtime_error("a node takes an object, not " + j.dump());
  Node read = read_node(j);
  node = std::move(read);
}

void to_json(nlohmann::json& j, const Node& node) {
  json out = json::object();
  out["id"] = node.id;
  out["type"] = node.type;
  out["params"] = params_json(node);
  if (!node.dynamicPins.empty()) {
    json pins = json::object();
    for (const auto& [setting, source] : node.dynamicPins) pins[setting] = json{{"ref", source}};
    out["dynamicPins"] = std::move(pins);
  }
  if (!node.comment.empty()) out["comment"] = node.comment;
  j = std::move(out);
}

} // namespace mforce
