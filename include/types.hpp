#pragma once

#include "config.hpp"

#include <cstdint>
#include <utility>
#include <concepts>

namespace PFnS {

enum NodeId : uint32_t {};
enum NumId : uint32_t {};
enum number_t : int64_t {};

number_t operator+(number_t a, number_t b) {
    return number_t(a + std::to_underlying(b));
}


}