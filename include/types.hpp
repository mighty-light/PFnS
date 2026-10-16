#pragma once

#ifdef SKEPTICAL
    #include <limits>
#endif
#include <cstdint>

namespace PFnS {

using endpoint_t = long long;
#ifdef SKEPTICAL
    constexpr auto endpoint_max = std::numeric_limits<endpoint_t>::max();
    constexpr auto endpoint_min = std::numeric_limits<endpoint_t>::min();
#endif


struct NodeId { 
    uint32_t v;
};

struct NumId {
    uint32_t v;
};

using number_t = long long;

}