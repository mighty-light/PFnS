#pragma once

#include <exception>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <utility>

namespace PFnS {

struct exception_with_location : public std::runtime_error {
    static std::string error_string(const std::source_location& loc, std::string_view err);

    exception_with_location(std::string_view err, const std::source_location& loc = std::source_location::current())
        : std::runtime_error(error_string(loc, err)) {}
};

}