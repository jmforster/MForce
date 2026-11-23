#pragma once
#include <functional>
#include <string>

namespace mforce {

struct Component;

// The table rule, the one every reader of a node's arrays asks, and the
// check of a record: for each array group of `node`, its columns are all
// absent or all present at one length. `length` gives a column's length, or
// -1 when the file or the record gives no array under that name. Returns the
// sentence's tail for the first group that breaks the rule, "table '<group>'
// takes its columns at one length, all present or all absent (got ...)",
// else empty. A caller puts the node's id and type in front of it.
std::string ragged_table_refusal(const Component& node,
                                 const std::function<int(const char* column)>& length);

} // namespace mforce
