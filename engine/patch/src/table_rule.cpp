#include "mforce/patch/table_rule.h"
#include "mforce/core/component.h"
#include <functional>
#include <string>
#include <string_view>

namespace mforce {

std::string ragged_table_refusal(const Component& node,
                                 const std::function<int(const char* column)>& length)
{
    const auto arrays = node.array_descriptors();
    for (size_t g = 0; g < arrays.size(); ++g) {
        if (!arrays[g].groupName) continue;
        const std::string_view group = arrays[g].groupName;
        bool first = true;                      // the group is checked at its first column
        for (size_t k = 0; k < g && first; ++k)
            first = !(arrays[k].groupName && group == arrays[k].groupName);
        if (!first) continue;
        std::string got;
        size_t columns = 0, present = 0;
        int firstLength = 0;
        bool oneLength = true;
        for (const auto& d : arrays) {
            if (!d.groupName || group != d.groupName) continue;
            ++columns;
            const int n = length(d.name);
            if (n >= 0) {
                if (present++ == 0) firstLength = n;
                else oneLength = oneLength && n == firstLength;
            }
            got += std::string(got.empty() ? "" : ", ") + d.name + " "
                 + (n >= 0 ? std::to_string(n) : std::string("absent"));
        }
        if (present != 0 && (present != columns || !oneLength))
            return "table '" + std::string(group) + "' takes its columns at one length, "
                   "all present or all absent (got " + got + ")";
    }
    return {};
}

} // namespace mforce
