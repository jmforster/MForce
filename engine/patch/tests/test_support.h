#pragma once
// Helpers the patch tests share, and the suites one file runs from another.
#include "mforce/testing/check.h"
#include "patch/tests/fixtures.h"
#include "mforce/patch/envelope_preset.h"
#include "mforce/patch/node.h"
#include "mforce/patch/patch.h"
#include "mforce/patch/patch_json.h"
#include <nlohmann/json.hpp>
#include <exception>
#include <string>

using namespace mforce;

namespace patch_codec {
using J = nlohmann::json;
// A patch read from JSON text through the codec; throws as from_json does.
inline Patch load_text(const std::string& text) { return patch_door::read_text(text); }
// The refusal reading `text` gives, whole; empty when it reads.
inline std::string refusal(const std::string& text) {
    try { (void)load_text(text); } catch (const std::exception& e) { return e.what(); }
    return {};
}
// A node read on its own.
inline Node read_node(const std::string& text) {
    Node n;
    from_json(J::parse(text), n);
    return n;
}
// An instrument of two nodes: a sine 'o' (the output) and `node`, graph.nodes[1].
inline std::string with_node(const std::string& node) {
    return R"J({"instrument":{},"graph":{"output":"o","nodes":[{"id":"o","type":"SineSource"},)J" + node + "]}}";
}
// `params` as the bag of a node 'id' of `type`, as with_node's graph.nodes[1].
inline std::string with_params(const char* id, const char* type, const std::string& params) {
    return with_node(std::string(R"J({"id":")J") + id + R"J(","type":")J" + type + R"J(","params":)J" + params + "}");
}
// Every stage column of an Envelope at two rows, with `type` as the type column.
inline std::string stage_columns(const std::string& type) {
    return R"J("startVal":[0,1],"endVal":[1,0],"power":[0,0],"percent":[0.2,0],"minSec":[0,0],"maxSec":[1,0],"nominal":[0,0],"type":)J" + type;
}
}  // namespace patch_codec
