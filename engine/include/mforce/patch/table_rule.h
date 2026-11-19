#pragma once
#include <functional>
#include <string>

namespace mforce {

struct Component;

// The table rule, the one both hosts ask: for each array group of `node`,
// its columns are all absent or all present at one length. `length` gives a
// column's length, or -1 when the file gives no array under that name.
// Returns the sentence's tail for the first group that breaks the rule,
// "table '<group>' takes its columns at one length, all present or all
// absent (got ...)", else empty. The loader and the editor throw it after
// the node's id and type.
std::string ragged_table_refusal(const Component& node,
                                 const std::function<int(const char* column)>& length);

} // namespace mforce
