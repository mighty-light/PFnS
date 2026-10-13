#pragma once

#include <limits>

namespace PFnS {

using endpoint_t = long long;
#ifdef SKEPTICAL
    constexpr auto endpoint_max = std::numeric_limits<endpoint_t>::max();
    constexpr auto endpoint_min = std::numeric_limits<endpoint_t>::min();
#endif

}