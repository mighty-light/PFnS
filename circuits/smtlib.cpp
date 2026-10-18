#include "smtlib.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iterator>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace PFnS
{
    namespace
    {
        [[nodiscard]] std::uint32_t raw(NodeId id)
        {
            return std::to_underlying(id);
        }

        [[nodiscard]] std::string integer_literal(number_t value)
        {
            const std::int64_t n = std::to_underlying(value);
            if (n >= 0)
                return std::to_string(static_cast<std::uint64_t>(n));

            // Avoid negating INT64_MIN directly.
            const std::uint64_t magnitude =
                static_cast<std::uint64_t>(-(n + 1)) + std::uint64_t{1};
            return "(- " + std::to_string(magnitude) + ")";
        }

        [[nodiscard]] bool is_symbol_initial(unsigned char ch)
        {
            if ((ch >= static_cast<unsigned char>('a') &&
                 ch <= static_cast<unsigned char>('z')) ||
                (ch >= static_cast<unsigned char>('A') &&
                 ch <= static_cast<unsigned char>('Z')))
                return true;

            constexpr std::string_view special = "~!@$%^&*_-+=<>.?/";
            return special.find(static_cast<char>(ch)) != std::string_view::npos;
        }

        [[nodiscard]] bool is_symbol_character(unsigned char ch)
        {
            return is_symbol_initial(ch) ||
                   (ch >= static_cast<unsigned char>('0') &&
                    ch <= static_cast<unsigned char>('9'));
        }

        [[nodiscard]] bool is_simple_symbol(std::string_view name)
        {
            if (name.empty() || !is_symbol_initial(static_cast<unsigned char>(name.front())))
                return false;
            return std::all_of(name.begin() + 1, name.end(), [](char ch)
                               { return is_symbol_character(static_cast<unsigned char>(ch)); });
        }

        [[nodiscard]] bool is_reserved_symbol(std::string_view name)
        {
            // These names are special forms, literals, or built-in operators in
            // the fragment emitted by this converter. Reject them for generated
            // binders/function names rather than producing ambiguous SMT-LIB.
            constexpr std::string_view reserved[] = {
                "true", "false", "let", "forall", "exists", "match", "par", "as",
                "_", "!", "and", "or", "not", "=>", "ite", "=", "distinct",
                ">", ">=", "<", "<=", "+", "-", "*", "div", "mod", "abs", "Int"};
            return std::find(std::begin(reserved), std::end(reserved), name) !=
                   std::end(reserved);
        }

        void validate_symbol(std::string_view name, std::string_view kind)
        {
            if (!is_simple_symbol(name) || is_reserved_symbol(name))
                throw std::invalid_argument(std::string(kind) + " '" + std::string(name) +
                                            "' is not a supported SMT-LIB symbol");
        }

        [[nodiscard]] std::vector<std::string> resolve_names(
            std::span<const std::string> supplied,
            std::size_t count,
            std::string_view prefix,
            std::string_view kind)
        {
            std::vector<std::string> names;
            names.reserve(count);

            if (supplied.empty())
            {
                for (std::size_t i = 0; i < count; ++i)
                    names.push_back(std::string(prefix) + std::to_string(i));
            }
            else
            {
                if (supplied.size() != count)
                    throw std::invalid_argument(std::string(kind) +
                                                " name count does not match the requested count");
                names.assign(supplied.begin(), supplied.end());
            }

            std::unordered_set<std::string> unique;
            for (const std::string &name : names)
            {
                validate_symbol(name, kind);
                if (!unique.insert(name).second)
                    throw std::invalid_argument(std::string("duplicate ") + std::string(kind) +
                                                " name: " + name);
            }
            return names;
        }

        [[nodiscard]] bool is_leaf(NodeType type)
        {
            return type == VARIABLE || type == CONST;
        }

        [[nodiscard]] std::vector<NodeId> reachable_computed_nodes(
            const Circuit &circuit, NodeId root)
        {
            const std::size_t node_count = circuit.size();
            std::vector<bool> seen(node_count, false);
            std::vector<NodeId> worklist{root};

            while (!worklist.empty())
            {
                const NodeId current = worklist.back();
                worklist.pop_back();
                const std::size_t index = static_cast<std::size_t>(raw(current));
                if (index >= node_count)
                    throw std::invalid_argument("circuit contains an invalid node id");
                if (seen[index])
                    continue;
                seen[index] = true;

                switch (circuit.type(current))
                {
                case VARIABLE:
                case CONST:
                    break;
                case LINEAR:
                    for (const LinTerm &term : circuit.terms(current))
                        worklist.push_back(term.child);
                    break;
                case MAX:
                case MIN:
                case EQC:
                    worklist.push_back(circuit.a(current));
                    worklist.push_back(circuit.b(current));
                    break;
                case DIV:
                    worklist.push_back(circuit.a(current));
                    break;
                default:
                    throw std::invalid_argument("circuit contains an unknown node type");
                }
            }

            // Circuit construction is topological: each child has a smaller id
            // than its parent. Scanning the arena therefore gives a valid order
            // for nested SMT-LIB let-bindings.
            std::vector<NodeId> result;
            for (std::size_t i = 0; i < node_count; ++i)
            {
                if (seen[i])
                {
                    const NodeId id{static_cast<std::uint32_t>(i)};
                    if (!is_leaf(circuit.type(id)))
                        result.push_back(id);
                }
            }
            return result;
        }

        class ExpressionPrinter
        {
        public:
            ExpressionPrinter(const Circuit &circuit,
                              const std::vector<std::string> &input_names,
                              std::string temporary_prefix)
                : circuit_(circuit), input_names_(input_names),
                  temporary_prefix_(std::move(temporary_prefix))
            {
            }

            [[nodiscard]] std::string reference(NodeId id) const
            {
                switch (circuit_.type(id))
                {
                case VARIABLE:
                {
                    const std::uint32_t index = circuit_.input_index(id);
                    if (static_cast<std::size_t>(index) >= input_names_.size())
                        throw std::invalid_argument("circuit variable index " +
                                                    std::to_string(index) +
                                                    " is >= input_count");
                    return input_names_[index];
                }
                case CONST:
                    return integer_literal(circuit_.constant_value(id));
                case LINEAR:
                case MAX:
                case MIN:
                case DIV:
                case EQC:
                    return temporary_name(id);
                default:
                    throw std::invalid_argument("circuit contains an unknown node type");
                }
            }

            [[nodiscard]] std::string temporary_name(NodeId id) const
            {
                return temporary_prefix_ + std::to_string(raw(id));
            }

            [[nodiscard]] std::string definition(NodeId id) const
            {
                switch (circuit_.type(id))
                {
                case LINEAR:
                {
                    std::string expression = "(+ " + integer_literal(circuit_.bias(id));
                    for (const LinTerm &term : circuit_.terms(id))
                    {
                        expression += " (* ";
                        expression += integer_literal(circuit_.coeff(term.coeff));
                        expression += " ";
                        expression += reference(term.child);
                        expression += ")";
                    }
                    expression += ")";
                    return expression;
                }
                case MAX:
                    return "(ite (>= " + reference(circuit_.a(id)) + " " +
                           reference(circuit_.b(id)) + ") " + reference(circuit_.a(id)) +
                           " " + reference(circuit_.b(id)) + ")";
                case MIN:
                    return "(ite (<= " + reference(circuit_.a(id)) + " " +
                           reference(circuit_.b(id)) + ") " + reference(circuit_.a(id)) +
                           " " + reference(circuit_.b(id)) + ")";
                case DIV:
                    return "(div " + reference(circuit_.a(id)) + " " +
                           integer_literal(circuit_.divisor(id)) + ")";
                case EQC:
                    return "(ite (= " + reference(circuit_.a(id)) + " 0) " +
                           reference(circuit_.b(id)) + " 0)";
                case VARIABLE:
                case CONST:
                    throw std::invalid_argument("cannot emit a leaf node as a let definition");
                default:
                    throw std::invalid_argument("circuit contains an unknown node type");
                }
            }

        private:
            const Circuit &circuit_;
            const std::vector<std::string> &input_names_;
            std::string temporary_prefix_;
        };

        [[nodiscard]] std::string choose_temporary_prefix(
            const std::vector<std::string> &input_names,
            const std::vector<NodeId> &outputs,
            std::size_t node_count)
        {
            // Pick a prefix that cannot collide with any bound input name or with
            // a name generated for a node in this circuit.
            for (std::size_t attempt = 0;; ++attempt)
            {
                const std::string prefix = "pfnsc_tmp_" + std::to_string(attempt) + "_";
                bool collision = false;
                for (const std::string &name : input_names)
                {
                    if (name.starts_with(prefix))
                    {
                        collision = true;
                        break;
                    }
                }
                if (collision)
                    continue;

                for (const NodeId output : outputs)
                {
                    if (static_cast<std::size_t>(raw(output)) >= node_count)
                        throw std::invalid_argument("circuit output contains an invalid node id");
                }
                return prefix;
            }
        }

        void emit_output_body(const Circuit &circuit,
                              NodeId root,
                              const ExpressionPrinter &printer,
                              std::ostream &out)
        {
            const std::vector<NodeId> computed = reachable_computed_nodes(circuit, root);
            std::size_t indent = 2;
            for (const NodeId id : computed)
            {
                out << std::string(indent, ' ') << "(let ((" << printer.temporary_name(id)
                    << ' ' << printer.definition(id) << "))\n";
                indent += 2;
            }

            out << std::string(indent, ' ') << printer.reference(root);
            // Close the let forms and the enclosing define-fun form.
            out << std::string(computed.size() + 1, ')') << '\n';
        }
    } // namespace

    void write_smtlib2(const Circuit &circuit,
                       std::size_t input_count,
                       std::ostream &out,
                       std::span<const std::string> input_names,
                       std::span<const std::string> output_names)
    {
        const std::vector<std::string> resolved_inputs =
            resolve_names(input_names, input_count, "x", "input");
        const std::vector<std::string> resolved_outputs =
            resolve_names(output_names, circuit.outputs().size(), "skolem_", "output");
        const std::string temporary_prefix =
            choose_temporary_prefix(resolved_inputs, circuit.outputs(), circuit.size());
        const ExpressionPrinter printer(circuit, resolved_inputs, temporary_prefix);

        // Validate reachable variable indexes before writing any output, so a
        // caller gets an exception instead of a partially emitted file.
        for (const NodeId root : circuit.outputs())
        {
            const std::vector<NodeId> computed = reachable_computed_nodes(circuit, root);
            for (const NodeId id : computed)
                static_cast<void>(printer.definition(id));
            static_cast<void>(printer.reference(root));
        }

        out << "(set-logic QF_NIA)\n\n";
        const std::vector<NodeId> &outputs = circuit.outputs();
        for (std::size_t i = 0; i < outputs.size(); ++i)
        {
            out << "(define-fun " << resolved_outputs[i] << " (";
            for (std::size_t j = 0; j < resolved_inputs.size(); ++j)
            {
                if (j != 0)
                    out << ' ';
                out << '(' << resolved_inputs[j] << " Int)";
            }
            out << ") Int\n";
            emit_output_body(circuit, outputs[i], printer, out);
            out << '\n';
        }
    }

    std::string to_smtlib2(const Circuit &circuit,
                           std::size_t input_count,
                           std::span<const std::string> input_names,
                           std::span<const std::string> output_names)
    {
        std::ostringstream out;
        write_smtlib2(circuit, input_count, out, input_names, output_names);
        return out.str();
    }
} // namespace PFnS
