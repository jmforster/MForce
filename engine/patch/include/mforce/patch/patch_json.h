#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include "mforce/patch/fragment.h"
#include "mforce/patch/graph.h"
#include "mforce/patch/instrument_block.h"
#include "mforce/patch/layout.h"
#include "mforce/patch/node.h"
#include "mforce/patch/patch.h"

namespace mforce {

// The codec: the one reader and the one writer of a patch file.
void to_json(nlohmann::json& j, const Node& node);                void from_json(const nlohmann::json& j, Node& node);
void to_json(nlohmann::json& j, const Graph& graph);              void from_json(const nlohmann::json& j, Graph& graph);
void to_json(nlohmann::json& j, const InstrumentBlock& block);    void from_json(const nlohmann::json& j, InstrumentBlock& block);
void to_json(nlohmann::json& j, const Layout& layout);            void from_json(const nlohmann::json& j, Layout& layout);
void to_json(nlohmann::json& j, const Fragment& fragment);        void from_json(const nlohmann::json& j, Fragment& fragment);
void to_json(nlohmann::json& j, const Patch& patch);              void from_json(const nlohmann::json& j, Patch& patch);
// A read succeeds whole or throws std::runtime_error naming the place and
// leaves its target as it was. from_json(Patch) ends by asking check().
Patch load_patch(const std::string& path);                        // the message begins with the path
void  save_patch(const std::string& path, const Patch& patch);    // two-space indent, LF, one final newline

} // namespace mforce
