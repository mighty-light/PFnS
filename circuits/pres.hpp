#pragma once

#include "../include/types.hpp"
#include "../include/error.hpp"
#include <cstdint>
#include <cstddef>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace PFnS
{

    enum NodeType : uint8_t
    {
        VARIABLE, // aux = input index
        CONST,    // aux = number id
        LINEAR,   // sum_i coeff_i * child_i + bias, count >= 1 (empty sums collapse to CONST)
        MAX,      // max(a, b), operands sorted by id
        MIN,      // min(a, b), operands sorted by id
        DIV,      // floor(a / k), k > 0 enforced by the builder
        EQC       // a == 0 ? b : 0   (a = guard, b = value; order matters)
    };

    // Field meaning by type:
    //   type      count   first          aux
    //   VARIABLE  -       0              input index
    //   CONST     -       0              number id
    //   LINEAR    #terms  offset in lin_ bias (NumId)
    //   MAX/MIN   -       child a        child b
    //   DIV       -       child a        divisor (NumId)
    //   EQC       -       child a (guard) child b (value)
    // Nodes are hash-consed: structurally equal nodes share one NodeId.
    struct Node
    {
        NodeType type;
        uint8_t flags;  // spare, must be 0 (not part of node identity)
        uint16_t count; // LINEAR term count; builder rejects > UINT16_MAX
        uint32_t first;
        uint32_t aux;
    };
    static_assert(sizeof(Node) == 12);
    static_assert(sizeof(NodeId) == 4);
    static_assert(sizeof(NumId) == 4);
    static_assert(std::is_trivially_copyable_v<Node>);

    struct LinTerm
    {
        NodeId child;
        NumId coeff;
    };
    static_assert(sizeof(LinTerm) == 8); // assumes 32-bit NodeId and NumId

    class Circuit
    {
    public:
        // Construction (children must already exist: ids stay topological)
        constexpr NodeId add_var(uint32_t input_index);
        constexpr NodeId add_const(number_t v);
        constexpr NodeId add_linear(std::span<const NodeId> ch,
                                    std::span<const number_t> co, number_t bias);
        constexpr NodeId add_max(NodeId a, NodeId b);
        constexpr NodeId add_min(NodeId a, NodeId b);
        constexpr NodeId add_div(NodeId a, number_t k); // asserts k > 0
        constexpr NodeId add_eqc(NodeId guard, NodeId value);

        // Queries
        // Spans/references returned here are invalidated by the next add_* call.
        constexpr size_t size() const { return nodes_.size(); }
        constexpr NodeType type(NodeId n) const { return nodes_[n].type; }
        constexpr NodeId a(NodeId n) const
        {
            const Node &x = nodes_[n];
            switch (x.type)
            {
#ifdef SKEPTICAL
            case VARIABLE:
            case CONST:
            case LINEAR:
                throw exception_with_location("Not MAX/MIN/DIV/EQC");
#endif
            default:
                return NodeId{x.first};
            }
        }
        constexpr NodeId b(NodeId n) const
        {
            const Node &x = nodes_[n];
            switch (x.type)
            {
#ifdef SKEPTICAL
            case VARIABLE:
            case CONST:
            case LINEAR:
            case DIV:
                throw exception_with_location("Not MAX/MIN/EQC");
#endif
            default:
                return NodeId{x.aux};
            }
        }

        constexpr std::span<const LinTerm> terms(NodeId n) const
        {
            skeptical_check(n < nodes_.size(), "Out of bounds!");
            const Node &x = nodes_[n];
            skeptical_check(x.type == LINEAR, "Not Linear");
            return {lin_.data() + x.first, x.count};
        }

        constexpr number_t bias(NodeId n) const
        {
            const Node &x = nodes_[n];
            skeptical_check(x.type == LINEAR, "Not Linear");
            skeptical_check(x.aux < nums_.size(), "Out of bounds!");
            return nums_[x.aux];
        } // LINEAR, resolves NumId via pool

        constexpr number_t coeff(NumId c) const
        {
            skeptical_check(c < nums_.size(), "Out of bounds!");
            return nums_[c];
        }
        constexpr number_t divisor(NodeId n) const
        {
            skeptical_check(n < nodes_.size(), "Out of bounds!");
            const uint32_t x = nodes_[n].aux;
            skeptical_check(x < nums_.size(), "Out of bounds!");
            return nums_[x];
        }

        void add_output(NodeId n)
        {

            skeptical_check(n < nodes_.size(), "Out of bounds!");
            outputs_.push_back(n);
        }
        constexpr const std::vector<NodeId> &outputs() const { return outputs_; }

    private:
        constexpr NumId intern(number_t v); // later: inline small / GMP pool

        // Hash-consing core. For a LINEAR candidate, its terms must already sit at
        // the tail of lin_ starting at n.first; on a hit they are removed again.
        constexpr NodeId find_or_insert(Node n);
        constexpr uint64_t hash_node(const Node &n) const;
        constexpr bool equal_nodes(const Node &x, const Node &y) const;
        constexpr void grow_table();

        std::vector<Node> nodes_;
        std::vector<LinTerm> lin_;
        std::vector<number_t> nums_;                       // number pool (deduped), swap for GMP later
        std::unordered_map<number_t, uint32_t> num_index_; // value -> index in nums_
        std::vector<NodeId> outputs_;
        // Hash-consing table: open addressing, linear probing, power-of-two size,
        // holds ids into nodes_. Empty slots hold the sentinel UINT32_MAX.
        std::vector<NodeId> table_;
        // Scratch for add_linear canonicalization (avoids a heap alloc per call).
        std::vector<std::pair<uint32_t, number_t>> scratch_;
    };

} // namespace PFnS
