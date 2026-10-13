#pragma once

#include <format>
#include "error.hpp"

namespace PFnS {

std::string exception_with_location::error_string(const std::source_location& loc, std::string_view err) {
    return std::format("[{}@{}:{}] {}", loc.function_name(), loc.file_name(), loc.line(), err);
}

}