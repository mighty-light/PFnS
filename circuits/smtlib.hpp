#pragma once

#include "pres.hpp"

#include <cstddef>
#include <iosfwd>
#include <span>
#include <string>

namespace PFnS
{
    // Emit one define-fun declaration for each Circuit output.
    //
    // If input_names is empty, inputs are named x0, x1, ... . Otherwise it must
    // contain exactly input_count valid SMT-LIB simple symbols.
    //
    // If output_names is empty, outputs are named skolem_0, skolem_1, ... .
    // Otherwise it must contain exactly circuit.outputs().size() valid symbols.
    //
    // The result uses let-bindings for reachable non-leaf nodes, preserving DAG
    // sharing rather than recursively expanding every occurrence. It emits the
    // declarations/definitions only; it deliberately does not append check-sat.
    void write_smtlib2(const Circuit &circuit,
                       std::size_t input_count,
                       std::ostream &out,
                       std::span<const std::string> input_names = {},
                       std::span<const std::string> output_names = {});

    [[nodiscard]] std::string to_smtlib2(
        const Circuit &circuit,
        std::size_t input_count,
        std::span<const std::string> input_names = {},
        std::span<const std::string> output_names = {});
} // namespace PFnS
