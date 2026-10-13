#pragma once

#include "../include/error.hpp"
#include "../include/types.hpp"
#include "../include/config.hpp"
#include <vector>
#include <compare>

namespace PFnS {

struct Interval {
    endpoint_t x;
    endpoint_t y;
    bool empty() const { return x > y; }
    bool normalize_if_empty() {
        if (empty()) {
            x = 1;
            y = 0;
            return true;
        }
        return false;
    }
    static bool coalescable(const Interval& one, const Interval& other);
    static Interval coalesce(const Interval& one, const Interval& other);
    
    // differs from paper in that empty < empty is false
    bool operator<(const Interval& other) const {
        if (other.empty())
            return false;
        if (empty())
            return true;
        if (!coalescable(*this, other) && !other.empty() && y < other.x)
            return true;
        return false;
    }
};

struct IntervalList {
    std::vector<Interval> finite;
    endpoint_t neg_infty_y;
    endpoint_t pos_infty_x;
};

}