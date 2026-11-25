#include "mforce/patch/fragment_text.h"
#include <stdexcept>
#include <string>
#include "mforce/patch/patch_json.h"

namespace mforce {

namespace {

using nlohmann::json;

constexpr const char* kMarker = "mforceClip";

}  // namespace

std::string fragment_text(const Fragment& fragment) {
  json j = json(fragment);
  j[kMarker] = 1;
  return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

Fragment fragment_from_text(std::string_view text) {
  json j = json::parse(text.begin(), text.end(), nullptr, false);   // false: a text that is not JSON is discarded, not thrown
  if (j.is_discarded() || !j.is_object() || !j.contains(kMarker) || j.at(kMarker) != 1)
    throw std::runtime_error("clipboard is not an MForce fragment");
  j.erase(kMarker);
  if (!j.contains("nodes") || (j.at("nodes").is_array() && j.at("nodes").empty()))
    throw std::runtime_error("fragment had no nodes");
  Fragment f;
  from_json(j, f);
  return f;
}

} // namespace mforce
