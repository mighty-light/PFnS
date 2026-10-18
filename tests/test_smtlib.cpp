#include "../circuits/smtlib.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace PFnS;

namespace
{
    void assert_balanced_parentheses(const std::string &text)
    {
        int depth = 0;
        for (const char ch : text)
        {
            if (ch == '(')
                ++depth;
            else if (ch == ')')
            {
                --depth;
                assert(depth >= 0);
            }
        }
        assert(depth == 0);
    }

    void test_all_node_kinds()
    {
        Circuit circuit;
        const NodeId x = circuit.add_var(0);
        const NodeId y = circuit.add_var(1);

        const std::array<NodeId, 2> children{x, y};
        const std::array<number_t, 2> coefficients{
            number_t{2}, number_t{-3}};
        const NodeId linear = circuit.add_linear(children, coefficients, number_t{7});
        const NodeId maximum = circuit.add_max(x, y);
        const NodeId minimum = circuit.add_min(x, y);
        const NodeId quotient = circuit.add_div(linear, number_t{3});
        const NodeId eqc = circuit.add_eqc(x, y);

        circuit.add_output(linear);
        circuit.add_output(maximum);
        circuit.add_output(minimum);
        circuit.add_output(quotient);
        circuit.add_output(eqc);

        const std::string smt = to_smtlib2(circuit, 2);
        assert_balanced_parentheses(smt);
        assert(smt.starts_with("(set-logic QF_NIA)\n\n"));
        assert(smt.find("(define-fun skolem_0 ((x0 Int) (x1 Int)) Int") !=
               std::string::npos);
        assert(smt.find("(+ 7 (* 2 x0) (* (- 3) x1))") != std::string::npos);
        assert(smt.find("(ite (>= x0 x1) x0 x1)") != std::string::npos);
        assert(smt.find("(ite (<= x0 x1) x0 x1)") != std::string::npos);
        assert(smt.find("(div pfnsc_tmp_0_2 3)") != std::string::npos);
        assert(smt.find("(ite (= x0 0) x1 0)") != std::string::npos);
        assert(smt.find("(check-sat)") == std::string::npos);
    }

    void test_custom_names_and_leaf_outputs()
    {
        Circuit circuit;
        const NodeId x = circuit.add_var(0);
        const NodeId seven = circuit.add_const(number_t{7});
        circuit.add_output(x);
        circuit.add_output(seven);

        const std::vector<std::string> input_names{"input"};
        const std::vector<std::string> output_names{"identity", "constant"};
        const std::string smt = to_smtlib2(circuit, 1, input_names, output_names);
        assert_balanced_parentheses(smt);

        assert(smt.find("(define-fun identity ((input Int)) Int\n  input)") !=
               std::string::npos);
        assert(smt.find("(define-fun constant ((input Int)) Int\n  7)") !=
               std::string::npos);
    }

    void test_negative_integer_literal_minimum()
    {
        Circuit circuit;
        const NodeId minimum = circuit.add_const(
            static_cast<number_t>(std::numeric_limits<std::int64_t>::min()));
        circuit.add_output(minimum);

        const std::string smt = to_smtlib2(circuit, 0);
        assert_balanced_parentheses(smt);
        assert(smt.find("(- 9223372036854775808)") != std::string::npos);
    }

    void test_rejects_bad_input_count_and_names()
    {
        Circuit circuit;
        const NodeId x1 = circuit.add_var(1);
        circuit.add_output(x1);

        bool threw = false;
        try
        {
            static_cast<void>(to_smtlib2(circuit, 1));
        }
        catch (const std::invalid_argument &)
        {
            threw = true;
        }
        assert(threw);

        Circuit valid;
        valid.add_output(valid.add_const(number_t{0}));
        const std::vector<std::string> duplicate_inputs{"same", "same"};
        threw = false;
        try
        {
            static_cast<void>(to_smtlib2(valid, 2, duplicate_inputs));
        }
        catch (const std::invalid_argument &)
        {
            threw = true;
        }
        assert(threw);
    }
} // namespace

int main()
{
    test_all_node_kinds();
    test_custom_names_and_leaf_outputs();
    test_negative_integer_literal_minimum();
    test_rejects_bad_input_count_and_names();
    std::cout << "test_smtlib: all tests passed\n";
}
