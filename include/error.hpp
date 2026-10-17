#pragma once

#include <exception>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>

namespace PFnS
{

    struct exception_with_location : public std::runtime_error
    {
        static constexpr std::string error_string(const std::source_location &loc, std::string_view err);

        constexpr exception_with_location(std::string_view err, const std::source_location &loc = std::source_location::current())
            : std::runtime_error(error_string(loc, err)) {}
    };

    void skeptical_check(bool chk, std::string_view err, const std::source_location &loc = std::source_location::current())
    {
#ifdef SKEPTICAL
        if (!chk)
            throw exception_with_location(err, loc);
#endif
    }

}