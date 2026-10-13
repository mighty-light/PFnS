#pragma once

#include "interval.hpp"

namespace PFnS {

bool Interval::coalescable(const Interval& one, const Interval& other) {
    #ifdef SKEPTICAL
        if (one.y == endpoint_max)
            throw PFnS::exception_with_location("");
        if (one.empty())
            throw PFnS::exception_with_location("");
        if (other.empty())
            throw PFnS::exception_with_location("");
    #endif
    return other.x <= (one.y + 1);
}

Interval Interval::coalesce(const Interval& one, const Interval& other) {
    #ifdef SKEPTICAL
        if (!coalescable(one, other))
            throw PFnS::exception_with_location("");
    #endif
    return { std::min(one.x, other.x), std::max(one.y, other.y) };
}

}